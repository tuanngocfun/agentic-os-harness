#include "syscall.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void syscall_init(void) {
    extern void isr_stub_128(void);
    extern void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
    idt_set_gate(0x80, (uint32_t)isr_stub_128, 0x08, 0xEE);
}

uint32_t syscall_handler(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2;
    (void)arg3;

    switch (syscall_num) {
        case SYS_PUTS:
            vga_puts((const char *)arg1);
            return 0;
        case SYS_GETCHAR:
            return (uint32_t)keyboard_getchar();
        case SYS_CLEAR:
            vga_clear();
            return 0;
        case SYS_UPTIME:
            return timer_get_ticks();
        default:
            return (uint32_t)-1;
    }
}
