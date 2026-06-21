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

static int is_user_range_valid(uint32_t ptr, uint32_t size) {
    if (ptr < USER_SPACE_START) return 0;
    if (ptr >= USER_SPACE_END) return 0;
    if (size > 0 && (ptr + size) > USER_SPACE_END) return 0;
    if (size > 0 && (ptr + size) < ptr) return 0;  // Overflow check
    return 1;
}

static int is_user_pointer_valid(uint32_t ptr, uint32_t size) {
    if (!is_user_range_valid(ptr, size)) return 0;

    // Check EVERY page in the range, not just first and last
    uint32_t start_page = ptr & ~0xFFF;
    uint32_t end_addr = ptr + (size > 0 ? size - 1 : 0);
    uint32_t end_page = end_addr & ~0xFFF;

    for (uint32_t page = start_page; page <= end_page; page += 0x1000) {
        if (!paging_is_mapped(page)) {
            return 0;
        }
        if (!paging_is_user_accessible(page)) {
            return 0;
        }
    }

    return 1;
}

static int is_user_pointer_writable(uint32_t ptr, uint32_t size) {
    if (!is_user_range_valid(ptr, size)) return 0;

    uint32_t start_page = ptr & ~0xFFF;
    uint32_t end_addr = ptr + (size > 0 ? size - 1 : 0);
    uint32_t end_page = end_addr & ~0xFFF;

    for (uint32_t page = start_page; page <= end_page; page += 0x1000) {
        if (!paging_is_user_writable(page)) {
            return 0;
        }
    }

    return 1;
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
    if (!dest || max_len == 0) {
        return VFS_EINVAL;
    }

    for (uint32_t i = 0; i < max_len; i++) {
        if (!is_user_pointer_valid(user_ptr + i, 1)) {
            return VFS_EINVAL;
        }
        dest[i] = *((const char *)(user_ptr + i));
        if (dest[i] == '\0') {
            return VFS_SUCCESS;
        }
    }

    dest[max_len - 1] = '\0';
    return VFS_EINVAL;
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

        if (!is_user_pointer_valid(slot, sizeof(uint32_t))) {
            return SYSCALL_EFAULT;
        }
        string_ptr = *((const uint32_t *)slot);
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

            if (!is_user_pointer_valid(string_ptr + length, 1)) {
                return SYSCALL_EFAULT;
            }
            if (vectors->bytes_used >= EXEC_MAX_VECTOR_BYTES) {
                return SYSCALL_E2BIG;
            }

            value = *((const char *)(string_ptr + length));
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
    uint32_t mapped_stack_phys;
    uint32_t old_cr3;
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
    paging_map_page_in_directory(new_cr3,
                                 stack_page,
                                 stack_phys,
                                 PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

    old_cr3 = paging_get_current_directory();
    paging_switch_directory(new_cr3);
    mapped_stack_phys = paging_get_physical_address(stack_page) & 0xFFFFF000;
    paging_switch_directory(old_cr3);
    if (mapped_stack_phys != stack_phys) {
        frame_free(stack_phys);
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

static uint32_t syscall_read(uint32_t fd, uint32_t buffer_ptr, uint32_t count, uint32_t caller_cs) {
    int status;
    int handle;
    struct process *current;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }

    if (count > 0 && !is_user_pointer_writable(buffer_ptr, count)) {
        return SYSCALL_EFAULT;
    }

    current = process_get_current();
    handle = process_fd_resolve(current, (int)fd);
    if (handle < 0) {
        return SYSCALL_EBADF;
    }

    status = vfs_read(handle, (void *)buffer_ptr, count);
    if (status < 0) {
        return syscall_from_vfs_status(status);
    }

    return (uint32_t)status;
}

static uint32_t syscall_write(uint32_t fd, uint32_t buffer_ptr, uint32_t count, uint32_t caller_cs) {
    int status;
    int handle;
    struct process *current;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }

    if (count > 0 && !is_user_pointer_valid(buffer_ptr, count)) {
        return SYSCALL_EFAULT;
    }

    current = process_get_current();
    handle = process_fd_resolve(current, (int)fd);
    if (handle < 0) {
        return SYSCALL_EBADF;
    }

    status = vfs_write(handle, (const void *)buffer_ptr, count);
    if (status < 0) {
        return syscall_from_vfs_status(status);
    }

    return (uint32_t)status;
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

    user_stat->size = stat.size;
    user_stat->start_sector = stat.start_sector;
    user_stat->allocated_sectors = stat.allocated_sectors;
    user_stat->flags = stat.flags;
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

#if defined(ENABLE_SYSCALL_NEGATIVE_SELFTEST) || defined(ENABLE_SYSCALL_FILE_SELFTEST) || defined(ENABLE_PROCESS_SYSCALL_SELFTEST) || defined(ENABLE_PROCESS_LIFECYCLE_SELFTEST) || defined(ENABLE_REDTEAM_SELFTEST) || defined(ENABLE_EXEC_ARGS_SELFTEST) || defined(ENABLE_VM_SELFTEST)
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
                default:
                    return SYSCALL_EINVAL;
            }
            return SYSCALL_SUCCESS;
        }
#endif

        case SYS_OPEN:
            return syscall_open(arg1, arg2, caller_cs);

        case SYS_READ:
            return syscall_read(arg1, arg2, arg3, caller_cs);

        case SYS_WRITE:
            return syscall_write(arg1, arg2, arg3, caller_cs);

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
        return control.resume_esp;
    }
    return frame_esp;
}
