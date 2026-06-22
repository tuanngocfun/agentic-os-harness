#include "syscall.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "serial.h"
#include "paging.h"
#include "vfs.h"
#include "process.h"
#include "scheduler.h"
#include "elf.h"
#include "frame.h"
#include "allocator.h"
#include "interrupt_frame.h"
#include "string.h"
#include "pipe.h"
#include "uaccess.h"
#include <stdint.h>


void syscall_init(void) {
    extern void isr_stub_128(void);
    extern void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
    idt_set_gate(0x80, (uint32_t)isr_stub_128, 0x08, 0xEE);
}

static int is_ring3(uint32_t caller_cs);

struct syscall_dispatch_control {
    struct interrupt_frame *frame;
    uint32_t resume_esp;
    int switch_requested;
};

#ifdef ENABLE_VM_SELFTEST
static uint32_t vm_test_free_after_demand;
#endif


static int is_user_pointer_valid(uint32_t ptr, uint32_t size) {
    return uaccess_readable(ptr, size);
}

static int is_user_pointer_writable(uint32_t ptr, uint32_t size) {
    return uaccess_prepare_write(ptr, size) == UACCESS_SUCCESS;
}


static uint32_t syscall_from_vfs_status(int status) {
    switch (status) {
        case VFS_SUCCESS:
            return SYSCALL_SUCCESS;
        case VFS_EINVAL:
            return SYSCALL_EINVAL;
        case VFS_ENOENT:
            return SYSCALL_ENOENT;
        case VFS_ENOSPC:
            return SYSCALL_ENOSPC;
        case VFS_EIO:
            return SYSCALL_EIO;
        case VFS_EMFILE:
            return SYSCALL_EMFILE;
        case VFS_EBADF:
            return SYSCALL_EBADF;
        case VFS_EEXIST:
            return SYSCALL_EEXIST;
        default:
            return SYSCALL_EIO;
    }
}

static int syscall_copy_user_string(char *dest, uint32_t user_ptr, uint32_t max_len) {
    return copy_string_from_user(dest, user_ptr, max_len) == UACCESS_SUCCESS ?
        VFS_SUCCESS : VFS_EINVAL;
}

struct exec_vectors {
    uint32_t argc;
    uint32_t envc;
    uint32_t bytes_used;
    uint16_t argv_offsets[EXEC_MAX_ARGS];
    uint16_t env_offsets[EXEC_MAX_ENVS];
    char data[EXEC_MAX_VECTOR_BYTES];
};

static uint32_t exec_copy_vector(struct exec_vectors *vectors,
                                 uint32_t user_vector,
                                 int environment) {
    uint16_t *offsets = environment ? vectors->env_offsets : vectors->argv_offsets;
    uint32_t limit = environment ? EXEC_MAX_ENVS : EXEC_MAX_ARGS;
    uint32_t *count = environment ? &vectors->envc : &vectors->argc;

    *count = 0;
    if (user_vector == 0) {
        return SYSCALL_SUCCESS;
    }

    for (uint32_t index = 0; index <= limit; index++) {
        uint32_t slot = user_vector + index * sizeof(uint32_t);
        uint32_t string_ptr;
        int terminated = 0;

        if (copy_from_user(&string_ptr, slot, sizeof(string_ptr)) !=
            UACCESS_SUCCESS) {
            return SYSCALL_EFAULT;
        }
        if (string_ptr == 0) {
            *count = index;
            return SYSCALL_SUCCESS;
        }
        if (index == limit) {
            return SYSCALL_E2BIG;
        }

        offsets[index] = (uint16_t)vectors->bytes_used;
        for (uint32_t length = 0; length < EXEC_MAX_STRING_BYTES; length++) {
            char value;

            if (vectors->bytes_used >= EXEC_MAX_VECTOR_BYTES) {
                return SYSCALL_E2BIG;
            }

            if (copy_from_user(&value, string_ptr + length, 1) !=
                UACCESS_SUCCESS) {
                return SYSCALL_EFAULT;
            }
            vectors->data[vectors->bytes_used++] = value;
            if (value == '\0') {
                terminated = 1;
                break;
            }
        }
        if (!terminated) {
            return SYSCALL_E2BIG;
        }
    }

    return SYSCALL_E2BIG;
}

static int exec_build_user_stack(uint32_t new_cr3,
                                 const struct exec_vectors *vectors,
                                 uint32_t stack_page,
                                 uint32_t *stack_pointer) {
    uint32_t old_cr3 = paging_get_current_directory();
    uint32_t argv_user[EXEC_MAX_ARGS];
    uint32_t env_user[EXEC_MAX_ENVS];
    uint32_t sp = USER_STACK_TOP;
    uint32_t table_words;
    uint32_t table_bytes;
    uint32_t table_sp;
    uint32_t *table;

    paging_switch_directory(new_cr3);
    if (!paging_is_user_writable(stack_page)) {
        paging_switch_directory(old_cr3);
        return 0;
    }
    memset((void *)stack_page, 0, USER_STACK_SIZE);

    for (uint32_t i = vectors->envc; i > 0; i--) {
        uint32_t index = i - 1;
        const char *value = &vectors->data[vectors->env_offsets[index]];
        uint32_t length = (uint32_t)strlen(value) + 1;

        if (length > sp - stack_page) {
            paging_switch_directory(old_cr3);
            return 0;
        }
        sp -= length;
        memcpy((void *)sp, value, length);
        env_user[index] = sp;
    }

    for (uint32_t i = vectors->argc; i > 0; i--) {
        uint32_t index = i - 1;
        const char *value = &vectors->data[vectors->argv_offsets[index]];
        uint32_t length = (uint32_t)strlen(value) + 1;

        if (length > sp - stack_page) {
            paging_switch_directory(old_cr3);
            return 0;
        }
        sp -= length;
        memcpy((void *)sp, value, length);
        argv_user[index] = sp;
    }

    table_words = 1 + vectors->argc + 1 + vectors->envc + 1;
    table_bytes = table_words * sizeof(uint32_t);
    if (table_bytes > sp - stack_page) {
        paging_switch_directory(old_cr3);
        return 0;
    }
    table_sp = (sp - table_bytes) & ~0x0Fu;
    if (table_sp < stack_page) {
        paging_switch_directory(old_cr3);
        return 0;
    }

    table = (uint32_t *)table_sp;
    *table++ = vectors->argc;
    for (uint32_t i = 0; i < vectors->argc; i++) {
        *table++ = argv_user[i];
    }
    *table++ = 0;
    for (uint32_t i = 0; i < vectors->envc; i++) {
        *table++ = env_user[i];
    }
    *table = 0;

    *stack_pointer = table_sp;
    paging_switch_directory(old_cr3);
    return 1;
}

static uint32_t syscall_exec(uint32_t path_ptr,
                             uint32_t argv_ptr,
                             uint32_t envp_ptr,
                             uint32_t caller_cs,
                             uint32_t frame_ptr,
                             struct syscall_dispatch_control *control) {
    char kernel_path[VFS_MAX_PATH];
    struct exec_vectors *vectors;
    struct process *current;
    struct elf_load_info info;
    uint32_t new_cr3;
    uint32_t stack_page = USER_STACK_TOP - USER_STACK_SIZE;
    uint32_t stack_phys;
    uint32_t old_process_cr3;
    uint32_t new_stack_pointer;
    uint32_t status;
    int elf_status;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }
    if (!frame_ptr) {
        return SYSCALL_EINVAL;
    }

    current = process_get_current();
    if (!current) {
        return SYSCALL_EINVAL;
    }
    if (!is_user_pointer_valid(path_ptr, 1) ||
        syscall_copy_user_string(kernel_path, path_ptr, sizeof(kernel_path)) != VFS_SUCCESS) {
        return SYSCALL_EFAULT;
    }

    vectors = (struct exec_vectors *)kmalloc(sizeof(*vectors));
    if (!vectors) {
        return SYSCALL_ENOMEM;
    }
    memset(vectors, 0, sizeof(*vectors));

    status = exec_copy_vector(vectors, argv_ptr, 0);
    if (status != SYSCALL_SUCCESS) {
        kfree(vectors);
        return status;
    }
    status = exec_copy_vector(vectors, envp_ptr, 1);
    if (status != SYSCALL_SUCCESS) {
        kfree(vectors);
        return status;
    }

    new_cr3 = paging_create_address_space();
    if (!new_cr3) {
        kfree(vectors);
        return SYSCALL_ENOMEM;
    }

    elf_status = elf_load_from_vfs_into(new_cr3, kernel_path, &info);
    if (elf_status != ELF_SUCCESS) {
        paging_destroy_address_space(new_cr3);
        kfree(vectors);
        if (elf_status == ELF_ENOENT) return SYSCALL_ENOENT;
        if (elf_status == ELF_EINVAL) return SYSCALL_EINVAL;
        if (elf_status == ELF_ENOSPC) return SYSCALL_ENOSPC;
        return SYSCALL_EIO;
    }

    stack_phys = frame_alloc();
    if (!stack_phys) {
        paging_destroy_address_space(new_cr3);
        kfree(vectors);
        return SYSCALL_ENOMEM;
    }
    if (paging_get_physical_address_in_directory(new_cr3, stack_page) != 0 ||
        !paging_map_page_in_directory(new_cr3, stack_page, stack_phys,
                                      PAGE_PRESENT | PAGE_WRITABLE |
                                      PAGE_USER)) {
        frame_release(stack_phys);
        paging_destroy_address_space(new_cr3);
        kfree(vectors);
        return SYSCALL_EIO;
    }

    if (!exec_build_user_stack(new_cr3, vectors, stack_page, &new_stack_pointer)) {
        paging_destroy_address_space(new_cr3);
        kfree(vectors);
        return SYSCALL_E2BIG;
    }

    serial_puts("PROCESS_EXEC_LOAD_OK\n");
    serial_puts("EXEC loaded entry=");
    serial_put_uint32(info.entry);
    serial_puts("\n");

    old_process_cr3 = current->cr3;
    process_fd_close_on_exec(current);
    current->cr3 = new_cr3;
    current->heap_start = PROCESS_HEAP_START;
    current->heap_end = PROCESS_HEAP_START;
    current->eip = info.entry;
    current->user_stack = (uint32_t *)new_stack_pointer;
    current->interrupt_frame = 1;
    kfree(vectors);

    paging_switch_directory(new_cr3);
    paging_destroy_address_space(old_process_cr3);

    if (control && control->frame) {
        struct interrupt_frame *frame = control->frame;
        frame->edi = 0;
        frame->esi = 0;
        frame->ebp = 0;
        frame->ebx = 0;
        frame->edx = 0;
        frame->ecx = 0;
        frame->gs = 0x23;
        frame->fs = 0x23;
        frame->es = 0x23;
        frame->ds = 0x23;
        frame->eip = info.entry;
        frame->cs = 0x1B;
        frame->eflags = 0x202;
        frame->user_esp = new_stack_pointer;
        frame->user_ss = 0x23;
    } else {
        *((uint32_t *)frame_ptr) = info.entry;
        ((uint32_t *)frame_ptr)[3] = new_stack_pointer;
    }

    return SYSCALL_SUCCESS;
}

static uint32_t syscall_open(uint32_t path_ptr, uint32_t flags, uint32_t caller_cs) {
    char path[VFS_MAX_PATH];
    uint32_t vfs_flags = 0;
    int status;
    int fd;
    struct process *current;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }

    if (flags & SYS_O_RDONLY) vfs_flags |= VFS_O_RDONLY;
    if (flags & SYS_O_WRONLY) vfs_flags |= VFS_O_WRONLY;
    if (flags & SYS_O_CREAT) vfs_flags |= VFS_O_CREAT;
    if (flags & SYS_O_TRUNC) vfs_flags |= VFS_O_TRUNC;

    if ((flags & ~(SYS_O_RDWR | SYS_O_CREAT | SYS_O_TRUNC | SYS_O_CLOEXEC)) != 0) {
        return SYSCALL_EINVAL;
    }

    current = process_get_current();
    if (!current) {
        return SYSCALL_EINVAL;
    }

    status = syscall_copy_user_string(path, path_ptr, sizeof(path));
    if (status != VFS_SUCCESS) {
        return SYSCALL_EFAULT;
    }

    status = vfs_open(path, vfs_flags);
    if (status < 0) {
        return syscall_from_vfs_status(status);
    }

    fd = process_fd_install(current, status,
                            (flags & SYS_O_CLOEXEC) ? PROCESS_FD_CLOEXEC : 0);
    if (fd < 0) {
        (void)vfs_close(status);
        return syscall_from_vfs_status(fd);
    }

    return (uint32_t)fd;
}

static uint32_t syscall_read(uint32_t fd, uint32_t buffer_ptr,
                             uint32_t count, uint32_t caller_cs,
                             struct syscall_dispatch_control *control) {
    enum process_fd_kind kind;
    struct process *current;
    int handle;
    int status;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }
    if (count > 0 && uaccess_prepare_write(buffer_ptr, count) !=
            UACCESS_SUCCESS) {
        return SYSCALL_EFAULT;
    }
    current = process_get_current();
    if (process_fd_get(current, (int)fd, &kind, &handle) != VFS_SUCCESS) {
        return SYSCALL_EBADF;
    }
    if (kind == PROCESS_FD_VFS) {
        status = vfs_read(handle, (void *)buffer_ptr, count);
        return status < 0 ? syscall_from_vfs_status(status) :
                            (uint32_t)status;
    }
    if (kind != PROCESS_FD_PIPE_READ) {
        return SYSCALL_EBADF;
    }
    status = pipe_read(handle, (void *)buffer_ptr, count);
    if (status != PIPE_WOULD_BLOCK) {
        return status < 0 ? SYSCALL_EBADF : (uint32_t)status;
    }
    if (!control || !control->frame) {
        return SYSCALL_EAGAIN;
    }
    process_begin_pipe_wait(current, BLOCK_PIPE_READ, (int)fd,
                            buffer_ptr, count);
    uint32_t resume_esp = scheduler_block_current((uint32_t)control->frame);
    if (resume_esp == (uint32_t)control->frame) {
        current->block_reason = BLOCK_NONE;
        return SYSCALL_EAGAIN;
    }
    control->switch_requested = 1;
    control->resume_esp = resume_esp;
    return SYSCALL_SUCCESS;
}

static uint32_t syscall_write(uint32_t fd, uint32_t buffer_ptr,
                              uint32_t count, uint32_t caller_cs,
                              struct syscall_dispatch_control *control) {
    enum process_fd_kind kind;
    struct process *current;
    int handle;
    int status;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }
    if (count > 0 && !uaccess_readable(buffer_ptr, count)) {
        return SYSCALL_EFAULT;
    }
    current = process_get_current();
    if (process_fd_get(current, (int)fd, &kind, &handle) != VFS_SUCCESS) {
        return SYSCALL_EBADF;
    }
    if (kind == PROCESS_FD_VFS) {
        status = vfs_write(handle, (const void *)buffer_ptr, count);
        return status < 0 ? syscall_from_vfs_status(status) :
                            (uint32_t)status;
    }
    if (kind != PROCESS_FD_PIPE_WRITE) {
        return SYSCALL_EBADF;
    }
    status = pipe_write(handle, (const void *)buffer_ptr, count);
    if (status == PIPE_BROKEN) {
        return SYSCALL_EPIPE;
    }
    if (status != PIPE_WOULD_BLOCK) {
        return status < 0 ? SYSCALL_EBADF : (uint32_t)status;
    }
    if (!control || !control->frame) {
        return SYSCALL_EAGAIN;
    }
    process_begin_pipe_wait(current, BLOCK_PIPE_WRITE, (int)fd,
                            buffer_ptr, count);
    uint32_t resume_esp = scheduler_block_current((uint32_t)control->frame);
    if (resume_esp == (uint32_t)control->frame) {
        current->block_reason = BLOCK_NONE;
        return SYSCALL_EAGAIN;
    }
    control->switch_requested = 1;
    control->resume_esp = resume_esp;
    return SYSCALL_SUCCESS;
}

static uint32_t syscall_close(uint32_t fd, uint32_t caller_cs) {
    struct process *current;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }

    current = process_get_current();
    return syscall_from_vfs_status(process_fd_close(current, (int)fd));
}

static uint32_t syscall_stat(uint32_t path_ptr, uint32_t stat_ptr, uint32_t caller_cs) {
    char path[VFS_MAX_PATH];
    struct vfs_stat stat;
    struct syscall_file_stat *user_stat = (struct syscall_file_stat *)stat_ptr;
    int status;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }

    if (!is_user_pointer_writable(stat_ptr, sizeof(*user_stat))) {
        return SYSCALL_EFAULT;
    }

    status = syscall_copy_user_string(path, path_ptr, sizeof(path));
    if (status != VFS_SUCCESS) {
        return SYSCALL_EFAULT;
    }

    status = vfs_stat(path, &stat);
    if (status != VFS_SUCCESS) {
        return syscall_from_vfs_status(status);
    }

    struct syscall_file_stat result = {
        stat.size,
        stat.start_sector,
        stat.allocated_sectors,
        stat.flags
    };
    if (copy_to_user(stat_ptr, &result, sizeof(result)) != UACCESS_SUCCESS) {
        return SYSCALL_EFAULT;
    }
    return SYSCALL_SUCCESS;
}

// Check if caller is from ring 3 (user mode)
static int is_ring3(uint32_t caller_cs) {
    return (caller_cs & 0x03) == 0x03;
}

static uint32_t syscall_handle(uint32_t syscall_num,
                               uint32_t arg1,
                               uint32_t arg2,
                               uint32_t arg3,
                               uint32_t caller_cs,
                               uint32_t frame_ptr,
                               struct syscall_dispatch_control *control) {
    // Validate syscall number
    if (syscall_num == 0 || syscall_num > SYS_MAX) {
        return SYSCALL_ENOSYS;
    }

    switch (syscall_num) {
        case SYS_PUTS: {
            const char *str = (const char *)arg1;
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }
            // Validate pointer is in user space
            if (!is_user_pointer_valid((uint32_t)str, 1)) {
                return SYSCALL_EFAULT;
            }
            // Basic string length check (prevent runaway)
            uint32_t len = 0;
            while (len < 4096) {
                if (!is_user_pointer_valid((uint32_t)(str + len), 1)) {
                    return SYSCALL_EFAULT;
                }
                if (str[len] == '\0') break;
                len++;
            }
            if (len >= 4096) {
                return SYSCALL_EFAULT;  // String too long or not null-terminated
            }
            vga_puts(str);
            return SYSCALL_SUCCESS;
        }
        case SYS_GETCHAR:
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }
            return (uint32_t)keyboard_getchar();

        case SYS_CLEAR:
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }
            vga_clear();
            return SYSCALL_SUCCESS;

        case SYS_UPTIME:
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }
            return timer_get_ticks();

        case SYS_ECHO:
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }
            return arg1;

#if defined(ENABLE_SYSCALL_ABI_SELFTEST) || defined(ENABLE_REDTEAM_SELFTEST)
        case SYS_TEST_ABI:
            if (is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }
            serial_puts("SYSCALL_ABI_OK:");
            if (arg1 == 0x11111111 && arg2 == 0x22222222 && arg3 == 0x33333333) {
                serial_puts("ARGS_OK");
            } else {
                serial_puts("ARGS_FAIL");
            }
            serial_puts("\n");
            return 0xDEADBEEF;

#endif

#ifdef ENABLE_USERMODE_SELFTEST
        case SYS_USERMODE_TEST:
            if (is_ring3(caller_cs) && arg1 == 0xCAFEBABE) {
                serial_puts("USERMODE_RING3_OK\n");
                return 0xC001C0DE;
            }
            serial_puts("USERMODE_RING3_FAIL\n");
            return SYSCALL_EPERM;

#endif

#if defined(ENABLE_SYSCALL_NEGATIVE_SELFTEST) || defined(ENABLE_SYSCALL_FILE_SELFTEST) || defined(ENABLE_PROCESS_SYSCALL_SELFTEST) || defined(ENABLE_PROCESS_LIFECYCLE_SELFTEST) || defined(ENABLE_REDTEAM_SELFTEST) || defined(ENABLE_EXEC_ARGS_SELFTEST) || defined(ENABLE_VM_SELFTEST) || defined(ENABLE_IPC_SELFTEST)
        case SYS_TEST_MARKER: {
            struct process *current = process_get_current();
            if (!is_ring3(caller_cs) ||
                !process_consume_test_marker(current, arg1)) {
                return SYSCALL_EPERM;
            }
            switch (arg1) {
                case SYSCALL_MARK_INVALID_NUM:
                    serial_puts("SYSCALL_INVALID_NUM_OK\n");
                    break;
                case SYSCALL_MARK_ZERO_NUM:
                    serial_puts("SYSCALL_ZERO_NUM_OK\n");
                    break;
                case SYSCALL_MARK_BAD_POINTER:
                    serial_puts("SYSCALL_BAD_POINTER_OK\n");
                    break;
                case SYSCALL_MARK_UNMAPPED_POINTER:
                    serial_puts("SYSCALL_UNMAPPED_POINTER_OK\n");
                    break;
                case SYSCALL_MARK_RING3_OK:
                    serial_puts("SYSCALL_RING3_OK\n");
                    break;
                case SYSCALL_MARK_DONE:
                    serial_puts("SYSCALL_NEGATIVE_OK\n");
                    break;
                case SYSCALL_MARK_FILE_OPEN:
                    serial_puts("SYSCALL_FILE_OPEN_OK\n");
                    break;
                case SYSCALL_MARK_FILE_WRITE:
                    serial_puts("SYSCALL_FILE_WRITE_OK\n");
                    break;
                case SYSCALL_MARK_FILE_READ:
                    serial_puts("SYSCALL_FILE_READ_OK\n");
                    break;
                case SYSCALL_MARK_FILE_STAT:
                    serial_puts("SYSCALL_FILE_STAT_OK\n");
                    break;
                case SYSCALL_MARK_FILE_NEGATIVE:
                    serial_puts("SYSCALL_FILE_NEGATIVE_OK\n");
                    break;
                case SYSCALL_MARK_FILE_DONE:
                    serial_puts("SYSCALL_FILE_OK\n");
                    break;
                case SYSCALL_MARK_FILE_OPEN_FAIL:
                    serial_puts("SYSCALL_FILE_OPEN_FAIL\n");
                    break;
                case SYSCALL_MARK_FILE_WRITE_FAIL:
                    serial_puts("SYSCALL_FILE_WRITE_FAIL\n");
                    break;
                case SYSCALL_MARK_FILE_READ_FAIL:
                    serial_puts("SYSCALL_FILE_READ_FAIL\n");
                    break;
                case SYSCALL_MARK_FILE_STAT_FAIL:
                    serial_puts("SYSCALL_FILE_STAT_FAIL\n");
                    break;
                case SYSCALL_MARK_FILE_NEG_FAIL:
                    serial_puts("SYSCALL_FILE_NEGATIVE_FAIL\n");
                    break;
                case SYSCALL_MARK_PROCESS_GETPID:
                    serial_puts("PROCESS_GETPID_OK\n");
                    break;
                case SYSCALL_MARK_PROCESS_BRK_QUERY:
                    serial_puts("PROCESS_BRK_QUERY_OK\n");
                    break;
                case SYSCALL_MARK_PROCESS_BRK_GROW:
                    serial_puts("PROCESS_BRK_GROW_OK\n");
                    break;
                case SYSCALL_MARK_PROCESS_BRK_RW:
                    serial_puts("PROCESS_BRK_RW_OK\n");
                    break;
                case SYSCALL_MARK_PROCESS_BRK_SHRINK:
                    serial_puts("PROCESS_BRK_SHRINK_OK\n");
                    break;
                case SYSCALL_MARK_PROCESS_WAIT_NEG:
                    serial_puts("PROCESS_WAIT_NEGATIVE_OK\n");
                    break;
                case SYSCALL_MARK_PROCESS_EXEC_ENTERED:
                    serial_puts("PROCESS_EXEC_ENTERED_OK\n");
                    break;
                case SYSCALL_MARK_PROCESS_DONE:
                    serial_puts("PROCESS_OK\n");
                    break;
                case SYSCALL_MARK_PROCESS_FAIL:
                    serial_puts("PROCESS_FAIL\n");
                    break;
#ifdef ENABLE_PROCESS_LIFECYCLE_SELFTEST
                case SYSCALL_MARK_LIFECYCLE_FORK_PARENT:
                    serial_puts("LIFECYCLE_FORK_PARENT_OK\n");
                    break;
                case SYSCALL_MARK_LIFECYCLE_FORK_CHILD:
                    serial_puts("LIFECYCLE_FORK_CHILD_OK\n");
                    break;
                case SYSCALL_MARK_LIFECYCLE_WAIT_REAP:
                    if (process_get_count() != 1) {
                        return SYSCALL_EINVAL;
                    }
                    serial_puts("LIFECYCLE_WAIT_REAP_OK\n");
                    break;
                case SYSCALL_MARK_LIFECYCLE_ISOLATION:
                    serial_puts("LIFECYCLE_FORK_ISOLATION_OK\n");
                    break;
                case SYSCALL_MARK_LIFECYCLE_EXEC_REPLACED:
                    serial_puts("LIFECYCLE_EXEC_REPLACEMENT_OK\n");
                    break;
                case SYSCALL_MARK_LIFECYCLE_DONE:
                    serial_puts("LIFECYCLE_OK\n");
                    break;
                case SYSCALL_MARK_LIFECYCLE_FAIL:
                    serial_puts("LIFECYCLE_FAIL\n");
                    break;
#endif
#ifdef ENABLE_VM_SELFTEST
                case SYSCALL_MARK_VM_DEMAND_ZERO: {
                    uint32_t phys = paging_get_physical_address(PROCESS_HEAP_START) &
                                    0xFFFFF000;
                    uint32_t flags =
                        paging_get_page_flags_in_directory(current->cr3,
                                                           PROCESS_HEAP_START);
                    if (!phys || !(flags & PAGE_WRITABLE) ||
                        !(flags & PAGE_USER) ||
                        frame_get_refcount(phys) != 1 ||
                        *(volatile uint32_t *)PROCESS_HEAP_START != 0) {
                        return SYSCALL_EINVAL;
                    }
                    vm_test_free_after_demand = frame_get_free_count();
                    serial_puts("VM_DEMAND_ZERO_OK\n");
                    break;
                }
                case SYSCALL_MARK_VM_COW_SHARED: {
                    uint32_t phys = paging_get_physical_address(PROCESS_HEAP_START) &
                                    0xFFFFF000;
                    uint32_t flags =
                        paging_get_page_flags_in_directory(current->cr3,
                                                           PROCESS_HEAP_START);
                    if (!phys || !(flags & PAGE_COW) ||
                        (flags & PAGE_WRITABLE) ||
                        frame_get_refcount(phys) != 2 ||
                        *(volatile uint32_t *)PROCESS_HEAP_START !=
                            0x13579BDFu) {
                        return SYSCALL_EINVAL;
                    }
                    serial_puts("VM_COW_SHARED_OK\n");
                    break;
                }
                case SYSCALL_MARK_VM_COW_SPLIT: {
                    uint32_t phys = paging_get_physical_address(PROCESS_HEAP_START) &
                                    0xFFFFF000;
                    uint32_t flags =
                        paging_get_page_flags_in_directory(current->cr3,
                                                           PROCESS_HEAP_START);
                    if (!phys || !(flags & PAGE_WRITABLE) ||
                        (flags & PAGE_COW) ||
                        frame_get_refcount(phys) != 1 ||
                        *(volatile uint32_t *)PROCESS_HEAP_START !=
                            0x2468ACE0u) {
                        return SYSCALL_EINVAL;
                    }
                    serial_puts("VM_COW_SPLIT_OK\n");
                    break;
                }
                case SYSCALL_MARK_VM_COW_ISOLATION: {
                    uint32_t phys = paging_get_physical_address(PROCESS_HEAP_START) &
                                    0xFFFFF000;
                    if (!phys || frame_get_refcount(phys) != 1 ||
                        *(volatile uint32_t *)PROCESS_HEAP_START !=
                            0x13579BDFu) {
                        return SYSCALL_EINVAL;
                    }
                    serial_puts("VM_COW_ISOLATION_OK\n");
                    break;
                }
                case SYSCALL_MARK_VM_FRAME_ACCOUNTING:
                    if (process_get_count() != 1 ||
                        frame_get_free_count() != vm_test_free_after_demand) {
                        return SYSCALL_EINVAL;
                    }
                    serial_puts("VM_FRAME_ACCOUNTING_OK\n");
                    break;
                case SYSCALL_MARK_VM_GUARD_TERMINATION:
                    if (process_get_count() != 1 ||
                        frame_get_free_count() != vm_test_free_after_demand) {
                        return SYSCALL_EINVAL;
                    }
                    serial_puts("VM_GUARD_TERMINATION_OK\n");
                    break;
                case SYSCALL_MARK_VM_DONE:
                    serial_puts("VM_OK\n");
                    break;
                case SYSCALL_MARK_VM_FAIL:
                    serial_puts("VM_FAIL\n");
                    break;
#endif
#ifdef ENABLE_EXEC_ARGS_SELFTEST
                case SYSCALL_MARK_EXEC_ARGS_NEGATIVE:
                    serial_puts("EXEC_ARGS_NEGATIVE_OK\n");
                    break;
                case SYSCALL_MARK_EXEC_ARGS_LAYOUT:
                    serial_puts("EXEC_ARGS_LAYOUT_OK\n");
                    break;
                case SYSCALL_MARK_EXEC_ARGS_CONTENT:
                    serial_puts("EXEC_ARGS_CONTENT_OK\n");
                    break;
                case SYSCALL_MARK_EXEC_ARGS_DONE:
                    serial_puts("EXEC_ARGS_OK\n");
                    break;
                case SYSCALL_MARK_EXEC_ARGS_FAIL:
                    serial_puts("EXEC_ARGS_FAIL\n");
                    break;
#endif
#ifdef ENABLE_REDTEAM_SELFTEST
                case SYSCALL_MARK_RED_MARKER_BLOCKED:
                    serial_puts("RED_MARKER_FORGERY_BLOCKED\n");
                    break;
                case SYSCALL_MARK_RED_MARKER_REPLAY_BLOCKED:
                    serial_puts("RED_MARKER_REPLAY_BLOCKED\n");
                    break;
                case SYSCALL_MARK_RED_EXEC_BLOCKED:
                    serial_puts("RED_EXEC_RESIDUAL_MAPPING_BLOCKED\n");
                    break;
                case SYSCALL_MARK_RED_EXEC_FD_BLOCKED:
                    serial_puts("RED_EXEC_FD_LEAK_BLOCKED\n");
                    break;
                case SYSCALL_MARK_RED_ELF_OVERLAP_BLOCKED:
                    serial_puts("RED_ELF_OVERLAP_BLOCKED\n");
                    break;
                case SYSCALL_MARK_RED_EXEC_FAILURE_CLEANUP_BLOCKED:
                    serial_puts("RED_EXEC_FAILURE_CLEANUP_BLOCKED\n");
                    break;
                case SYSCALL_MARK_RED_PROCESS_DESTROY_CLEANUP_BLOCKED:
                    serial_puts("RED_PROCESS_DESTROY_CLEANUP_BLOCKED\n");
                    break;
                case SYSCALL_MARK_RED_SYSCALL_PRIV_BLOCKED:
                    serial_puts("RED_SYSCALL_PRIVILEGE_BLOCKED\n");
                    break;
                case SYSCALL_MARK_RED_DEFENSES_DONE:
                    serial_puts("RED_DEFENSES_OK\n");
                    break;
                case SYSCALL_MARK_RED_FAIL:
                    serial_puts("RED_TEAM_FAIL\n");
                    break;
#endif
#ifdef ENABLE_IPC_SELFTEST
                case SYSCALL_MARK_WAITPID_WNOHANG:
                    serial_puts("WAITPID_WNOHANG_OK\n");
                    break;
                case SYSCALL_MARK_WAITPID_SPECIFIC:
                    serial_puts("WAITPID_SPECIFIC_OK\n");
                    break;
                case SYSCALL_MARK_WAITPID_STATUS:
                    serial_puts("WAITPID_STATUS_OK\n");
                    break;
                case SYSCALL_MARK_WAITPID_NEGATIVE:
                    serial_puts("WAITPID_NEGATIVE_OK\n");
                    break;
                case SYSCALL_MARK_SIGNAL_KILL:
                    serial_puts("SIGNAL_KILL_OK\n");
                    break;
                case SYSCALL_MARK_SIGNAL_IGN:
                    serial_puts("SIGNAL_IGN_OK\n");
                    break;
                case SYSCALL_MARK_PIPE_CREATE:
                    serial_puts("PIPE_CREATE_OK\n");
                    break;
                case SYSCALL_MARK_PIPE_IO:
                    serial_puts("PIPE_IO_OK\n");
                    break;
                case SYSCALL_MARK_PIPE_EOF:
                    serial_puts("PIPE_EOF_OK\n");
                    break;
                case SYSCALL_MARK_IPC_DONE:
                    serial_puts("IPC_OK\n");
                    break;
                case SYSCALL_MARK_IPC_FAIL:
                    serial_puts("IPC_FAIL\n");
                    break;
#endif
                default:
                    return SYSCALL_EINVAL;
            }
            return SYSCALL_SUCCESS;
        }
#endif

        case SYS_OPEN:
            return syscall_open(arg1, arg2, caller_cs);

        case SYS_READ:
            return syscall_read(arg1, arg2, arg3, caller_cs, control);

        case SYS_WRITE:
            return syscall_write(arg1, arg2, arg3, caller_cs, control);

        case SYS_CLOSE:
            return syscall_close(arg1, caller_cs);

        case SYS_STAT:
            return syscall_stat(arg1, arg2, caller_cs);

        case SYS_GETPID: {
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *current = process_get_current();
            return current ? current->pid : 0;
        }

        case SYS_EXIT: {
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *current = process_get_current();
            if (!current) {
                return SYSCALL_EINVAL;
            }

            if (!control || !control->frame) {
                return SYSCALL_EINVAL;
            }

            process_mark_exit(current, arg1);
            control->switch_requested = 1;
            control->resume_esp =
                scheduler_exit_current((uint32_t)control->frame);
            return SYSCALL_SUCCESS;
        }

        case SYS_WAIT: {
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *current = process_get_current();
            if (!current) {
                return SYSCALL_EINVAL;
            }

            if (arg1 && !is_user_pointer_writable(arg1, sizeof(uint32_t))) {
                return SYSCALL_EFAULT;
            }

            struct process *child = process_find_exited_child(current->pid);
            if (child) {
                if (arg1) {
                    *((uint32_t *)arg1) = child->exit_code;
                }

                child->waited = 1;
                uint32_t child_pid = child->pid;
                process_destroy(child);
                return child_pid;
            }

            if (!process_has_child(current->pid)) {
                return SYSCALL_ECHILD;
            }

            if (!control || !control->frame) {
                return SYSCALL_EAGAIN;
            }

            process_begin_wait(current, arg1);
            uint32_t resume_esp =
                scheduler_block_current((uint32_t)control->frame);
            if (resume_esp == (uint32_t)control->frame) {
                process_cancel_wait(current);
                return SYSCALL_EAGAIN;
            }

            control->switch_requested = 1;
            control->resume_esp = resume_esp;
            return SYSCALL_SUCCESS;
        }

        case SYS_FORK: {
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            if (!control || !control->frame) {
                return SYSCALL_EINVAL;
            }

            struct process *current = process_get_current();
            if (!current) {
                return SYSCALL_EINVAL;
            }

            struct process *child = process_fork(current, control->frame);
            if (!child) {
                return SYSCALL_ENOMEM;
            }

            scheduler_add(child);
            return child->pid;
        }

        case SYS_EXEC:
            return syscall_exec(arg1, arg2, arg3, caller_cs, frame_ptr, control);

        case SYS_BRK: {
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *current = process_get_current();
            if (!current) {
                return SYSCALL_EINVAL;
            }

            uint32_t requested_brk = arg1;

            // Query current brk
            if (requested_brk == 0) {
                return current->heap_end;
            }

            // Validate request
            if (requested_brk < current->heap_start || requested_brk > PROCESS_HEAP_LIMIT) {
                return current->heap_end;  // Return current unchanged
            }

            uint32_t old_end_page = (current->heap_end + 0xFFF) & 0xFFFFF000;
            uint32_t new_end_page = (requested_brk + 0xFFF) & 0xFFFFF000;

            /* Growth is lazy. Shrink releases only pages already faulted in. */
            if (new_end_page < old_end_page) {
                for (uint32_t addr = new_end_page; addr < old_end_page;
                     addr += 0x1000) {
                    uint32_t phys = paging_unmap_page_in_directory(current->cr3, addr);
                    if (phys) {
                        frame_release(phys);
                    }
                }
            }

            // Update heap end
            current->heap_end = requested_brk;
            return requested_brk;
        }

        case SYS_WAITPID: {
            /* SYS_WAITPID(pid, status_ptr, options)
             * arg1 = target PID (-1 = any child, >0 = specific child)
             * arg2 = user pointer to uint32_t status (or 0 to skip)
             * arg3 = options (WNOHANG)
             */
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *current = process_get_current();
            if (!current) {
                return SYSCALL_EINVAL;
            }

            uint32_t target_pid = arg1;
            uint32_t options = arg3;

            /* Validate options: only WNOHANG is supported */
            if (options & ~WNOHANG) {
                return SYSCALL_EINVAL;
            }

            if (arg2 && !is_user_pointer_writable(arg2, sizeof(uint32_t))) {
                return SYSCALL_EFAULT;
            }

            /* Try to find an already-exited child matching the target */
            struct process *child = process_find_exited_child_by_pid(
                current->pid, target_pid);
            if (child) {
                uint32_t encoded_status;
                if (child->killed_by_signal) {
                    encoded_status = W_EXITCODE(0, child->killed_by_signal);
                } else {
                    encoded_status = W_EXITCODE(child->exit_code, 0);
                }
                if (arg2) {
                    *((uint32_t *)arg2) = encoded_status;
                }
                child->waited = 1;
                uint32_t child_pid = child->pid;
                process_destroy(child);
                return child_pid;
            }

            /* Check if the target child exists at all */
            if (target_pid != (uint32_t)-1) {
                struct process *target = process_get_by_pid(target_pid);
                if (!target || target->parent_pid != current->pid) {
                    return SYSCALL_ECHILD;
                }
            } else if (!process_has_child(current->pid)) {
                return SYSCALL_ECHILD;
            }

            /* WNOHANG: return 0 immediately if no child has exited */
            if (options & WNOHANG) {
                return 0;
            }

            /* Block until a matching child exits */
            if (!control || !control->frame) {
                return SYSCALL_EAGAIN;
            }

            process_begin_waitpid(current, arg2, target_pid);
            current->wait_is_waitpid = 1;
            uint32_t resume_esp =
                scheduler_block_current((uint32_t)control->frame);
            if (resume_esp == (uint32_t)control->frame) {
                process_cancel_wait(current);
                return SYSCALL_EAGAIN;
            }

            control->switch_requested = 1;
            control->resume_esp = resume_esp;
            return SYSCALL_SUCCESS;
        }

        case SYS_PIPE: {
            /* SYS_PIPE(read_fd_out, write_fd_out)
             * arg1 = user pointer to uint32_t (read end)
             * arg2 = user pointer to uint32_t (write end)
             */
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *current = process_get_current();
            if (!current) {
                return SYSCALL_EINVAL;
            }

            if (!arg1 || !is_user_pointer_writable(arg1, sizeof(uint32_t)) ||
                !arg2 || !is_user_pointer_writable(arg2, sizeof(uint32_t))) {
                return SYSCALL_EFAULT;
            }

            int read_handle, write_handle;
            if (pipe_create(&read_handle, &write_handle) != 0) {
                return SYSCALL_EMFILE;
            }

            int read_fd = process_fd_install_typed(current,
                PROCESS_FD_PIPE_READ, read_handle, 0);
            if (read_fd < 0) {
                pipe_close(read_handle);
                pipe_close(write_handle);
                return SYSCALL_EMFILE;
            }

            int write_fd = process_fd_install_typed(current,
                PROCESS_FD_PIPE_WRITE, write_handle, 0);
            if (write_fd < 0) {
                process_fd_close(current, read_fd);
                pipe_close(write_handle);
                return SYSCALL_EMFILE;
            }

            uint32_t r_fd = (uint32_t)read_fd;
            uint32_t w_fd = (uint32_t)write_fd;
            if (copy_to_user(arg1, &r_fd, sizeof(uint32_t)) != UACCESS_SUCCESS ||
                copy_to_user(arg2, &w_fd, sizeof(uint32_t)) != UACCESS_SUCCESS) {
                process_fd_close(current, read_fd);
                process_fd_close(current, write_fd);
                return SYSCALL_EFAULT;
            }
            return SYSCALL_SUCCESS;
        }

        case SYS_KILL: {
            /* SYS_KILL(pid, signum)
             * arg1 = target process PID
             * arg2 = signal number
             */
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *target = process_get_by_pid(arg1);
            if (!target) {
                return SYSCALL_ESRCH;
            }

            if (arg2 == 0 || arg2 >= NSIG) {
                return SYSCALL_EINVAL;
            }

            if (process_send_signal(target, arg2) != 0) {
                return SYSCALL_ESRCH;
            }
            return SYSCALL_SUCCESS;
        }

        case SYS_SIGPENDING: {
            /* SYS_SIGPENDING()
             * Returns bitmask of pending signals for the current process.
             */
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *current = process_get_current();
            if (!current) {
                return SYSCALL_EINVAL;
            }

            return current->pending_signals;
        }

        default:
            return SYSCALL_ENOSYS;
    }
}

uint32_t syscall_handler(uint32_t syscall_num,
                         uint32_t arg1,
                         uint32_t arg2,
                         uint32_t arg3,
                         uint32_t caller_cs,
                         uint32_t frame_ptr) {
    return syscall_handle(syscall_num, arg1, arg2, arg3,
                          caller_cs, frame_ptr, NULL);
}

uint32_t syscall_dispatch(uint32_t frame_esp) {
    struct interrupt_frame *frame = (struct interrupt_frame *)frame_esp;
    struct syscall_dispatch_control control;
    struct process *current;
    uint32_t result;

    if (!frame) {
        return 0;
    }

    control.frame = frame;
    control.resume_esp = frame_esp;
    control.switch_requested = 0;

    current = process_get_current();
    if (current) {
        current->interrupt_frame = 1;
    }

    result = syscall_handle(frame->eax,
                            frame->ebx,
                            frame->ecx,
                            frame->edx,
                            frame->cs,
                            (uint32_t)&frame->eip,
                            &control);
    frame->eax = result;

    if (control.switch_requested) {
        return process_deliver_signals(control.resume_esp);
    }
    return process_deliver_signals(frame_esp);
}
