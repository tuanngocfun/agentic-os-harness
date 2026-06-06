#include "syscall.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "serial.h"
#include "paging.h"
#include <stdint.h>

void syscall_init(void) {
    extern void isr_stub_128(void);
    extern void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
    idt_set_gate(0x80, (uint32_t)isr_stub_128, 0x08, 0xEE);
}

static int is_user_range_valid(uint32_t ptr, uint32_t size) {
    if (ptr < USER_SPACE_START) return 0;
    if (ptr >= USER_SPACE_END) return 0;
    if (size > 0 && (ptr + size) > USER_SPACE_END) return 0;
    if (size > 0 && (ptr + size) < ptr) return 0;  // Overflow check
    return 1;
}

static int is_user_pointer_valid(uint32_t ptr, uint32_t size) {
    if (!is_user_range_valid(ptr, size)) return 0;
    if (!paging_is_user_accessible(ptr)) return 0;
    if (size > 1 && !paging_is_user_accessible(ptr + size - 1)) return 0;
    return 1;
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
                default:
                    return SYSCALL_EINVAL;
            }
            return SYSCALL_SUCCESS;

        default:
            return SYSCALL_ENOSYS;
    }
}
