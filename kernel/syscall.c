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
#include <stdint.h>

void syscall_init(void) {
    extern void isr_stub_128(void);
    extern void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
    idt_set_gate(0x80, (uint32_t)isr_stub_128, 0x08, 0xEE);
}

static int is_ring3(uint32_t caller_cs);

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

static uint32_t syscall_open(uint32_t path_ptr, uint32_t flags, uint32_t caller_cs) {
    char path[VFS_MAX_PATH];
    uint32_t vfs_flags = 0;
    int status;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }

    if (flags & SYS_O_RDONLY) vfs_flags |= VFS_O_RDONLY;
    if (flags & SYS_O_WRONLY) vfs_flags |= VFS_O_WRONLY;
    if (flags & SYS_O_CREAT) vfs_flags |= VFS_O_CREAT;
    if (flags & SYS_O_TRUNC) vfs_flags |= VFS_O_TRUNC;

    if ((flags & ~(SYS_O_RDWR | SYS_O_CREAT | SYS_O_TRUNC)) != 0) {
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

    return (uint32_t)status;
}

static uint32_t syscall_read(uint32_t fd, uint32_t buffer_ptr, uint32_t count, uint32_t caller_cs) {
    int status;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }

    if (count > 0 && !is_user_pointer_writable(buffer_ptr, count)) {
        return SYSCALL_EFAULT;
    }

    status = vfs_read((int)fd, (void *)buffer_ptr, count);
    if (status < 0) {
        return syscall_from_vfs_status(status);
    }

    return (uint32_t)status;
}

static uint32_t syscall_write(uint32_t fd, uint32_t buffer_ptr, uint32_t count, uint32_t caller_cs) {
    int status;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }

    if (count > 0 && !is_user_pointer_valid(buffer_ptr, count)) {
        return SYSCALL_EFAULT;
    }

    status = vfs_write((int)fd, (const void *)buffer_ptr, count);
    if (status < 0) {
        return syscall_from_vfs_status(status);
    }

    return (uint32_t)status;
}

static uint32_t syscall_close(uint32_t fd, uint32_t caller_cs) {
    int status;

    if (!is_ring3(caller_cs)) {
        return SYSCALL_EPERM;
    }

    status = vfs_close((int)fd);
    return syscall_from_vfs_status(status);
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

uint32_t syscall_handler(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t caller_cs) {
    // Validate syscall number
    if (syscall_num == 0 || syscall_num > SYS_MAX) {
        return SYSCALL_ENOSYS;
    }

    switch (syscall_num) {
        case SYS_PUTS: {
            const char *str = (const char *)arg1;
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
            return (uint32_t)keyboard_getchar();

        case SYS_CLEAR:
            vga_clear();
            return SYSCALL_SUCCESS;

        case SYS_UPTIME:
            return timer_get_ticks();

        case SYS_ECHO:
            return arg1;

        case SYS_TEST_ABI:
            serial_puts("SYSCALL_ABI_OK:");
            if (arg1 == 0x11111111 && arg2 == 0x22222222 && arg3 == 0x33333333) {
                serial_puts("ARGS_OK");
            } else {
                serial_puts("ARGS_FAIL");
            }
            serial_puts("\n");
            return 0xDEADBEEF;

        case SYS_USERMODE_TEST:
            if (is_ring3(caller_cs) && arg1 == 0xCAFEBABE) {
                serial_puts("USERMODE_RING3_OK\n");
                return 0xC001C0DE;
            }
            serial_puts("USERMODE_RING3_FAIL\n");
            return SYSCALL_EPERM;

        case SYS_TEST_MARKER:
            if (!is_ring3(caller_cs)) {
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
                default:
                    return SYSCALL_EINVAL;
            }
            return SYSCALL_SUCCESS;

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

            // Mark process as exited with exit code
            current->exit_code = arg1;
            current->exited = 1;
            current->state = PROCESS_DEAD;

            // Free kernel stack
            if (current->kernel_stack) {
                // Stack will be freed by process_destroy
            }

            // Schedule next process - this never returns
            scheduler_schedule();

            // Should never reach here
            while (1) { asm volatile("hlt"); }
        }

        case SYS_WAIT: {
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *current = process_get_current();
            if (!current) {
                return (uint32_t)-1;
            }

            // Look for exited child processes
            for (int i = 0; i < MAX_PROCESSES; i++) {
                struct process *child = process_get_by_pid(i);
                if (child && child->parent_pid == current->pid &&
                    child->exited && !child->waited) {

                    // Copy exit code to user space if requested
                    uint32_t *status_ptr = (uint32_t *)arg1;
                    if (status_ptr && is_user_pointer_writable((uint32_t)status_ptr, 4)) {
                        *status_ptr = child->exit_code;
                    }

                    child->waited = 1;
                    uint32_t child_pid = child->pid;

                    // Cleanup zombie process
                    process_destroy(child);

                    return child_pid;
                }
            }

            // No exited children found
            return (uint32_t)-1;  // ECHILD
        }

        case SYS_FORK: {
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            struct process *parent = process_get_current();
            if (!parent) {
                return (uint32_t)-1;
            }

            // Create new process with same entry point
            struct process *child = process_create(parent->eip, 1);
            if (!child) {
                return (uint32_t)-1;  // ENOMEM
            }

            // Set up parent-child relationship
            child->parent_pid = parent->pid;

            // For now: share address space (simplified fork)
            // TODO: Implement proper copy-on-write or full page copy
            child->cr3 = parent->cr3;

            // Copy parent's register state so child resumes at same point
            // In a real fork, we'd need to copy the entire interrupt frame
            // For now, child will return 0 from syscall, parent gets child PID

            // Mark child as ready to run
            child->state = PROCESS_READY;

            // Add child to scheduler
            extern void scheduler_add(struct process *proc);
            scheduler_add(child);

            // Parent returns child PID, child will return 0
            // (Child's return value would be set when it actually runs)
            return child->pid;
        }

        case SYS_EXEC: {
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            // Validate path pointer
            const char *path = (const char *)arg1;
            if (!is_user_pointer_valid((uint32_t)path, 1)) {
                return SYSCALL_EFAULT;
            }

            // Copy path to kernel space (max 256 bytes)
            char kernel_path[256];
            int i;
            for (i = 0; i < 255; i++) {
                if (!is_user_pointer_valid((uint32_t)(path + i), 1)) {
                    return SYSCALL_EFAULT;
                }
                kernel_path[i] = path[i];
                if (kernel_path[i] == '\0') break;
            }
            kernel_path[255] = '\0';

            // Load ELF from VFS
            struct elf_load_info info;
            int ret = elf_load_from_vfs(kernel_path, &info);
            if (ret != ELF_SUCCESS) {
                // Return appropriate error
                if (ret == ELF_ENOENT) return SYSCALL_ENOENT;
                if (ret == ELF_EINVAL) return SYSCALL_EINVAL;
                return SYSCALL_EIO;
            }

            // ELF is now loaded into current address space
            // We need to jump to the entry point
            // This is tricky - we need to modify the interrupt frame
            // For now, just return the entry point and let user code handle it
            // A proper implementation would modify the saved EIP in the interrupt frame

            serial_puts("EXEC loaded entry=");
            serial_put_uint32(info.entry);
            serial_puts("\n");

            // TODO: Properly jump to entry point by modifying interrupt frame
            // For now, return entry point (user code must jump manually)
            return info.entry;
        }

        case SYS_BRK: {
            if (!is_ring3(caller_cs)) {
                return SYSCALL_EPERM;
            }

            // For now: return a fixed heap region
            // TODO: Implement dynamic heap growth
            // This would require tracking per-process heap boundary
            // and allocating/freeing pages as needed

            uint32_t requested_brk = arg1;
            uint32_t heap_start = 0x50000000;  // Fixed heap start
            uint32_t heap_limit = 0x60000000;  // Fixed heap limit (256MB)

            if (requested_brk == 0) {
                // Query current brk
                return heap_start;
            }

            if (requested_brk < heap_start || requested_brk > heap_limit) {
                // Invalid brk value
                return heap_start;  // Return current brk unchanged
            }

            // TODO: Actually allocate/free pages between old and new brk
            // For now, just return the requested value
            return requested_brk;
        }

        default:
            return SYSCALL_ENOSYS;
    }
}
