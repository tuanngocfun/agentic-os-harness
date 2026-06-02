#include "kernel.h"
#include "serial.h"
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "timer.h"
#include "memory.h"
#include "paging.h"
#include "tss.h"
#include "syscall.h"
#include "process.h"
#include "scheduler.h"
#include "shell.h"

void kernel_main(void) {
    serial_init();
    serial_puts("KERNEL_INIT_OK\n");

    vga_init();
    vga_puts("Kernel initialized successfully!\n");

    gdt_init();
    idt_init();
    timer_init(100);
    keyboard_init();
    memory_init();
    paging_init();
    tss_init();
    syscall_init();

#ifdef ENABLE_SYSCALL_ABI_SELFTEST
    {
        uint32_t result;
        asm volatile(
            "int $0x80"
            : "=a"(result)
            : "a"(6), "b"(0x11111111), "c"(0x22222222), "d"(0x33333333)
        );
        (void)result;
    }
#endif

#ifdef ENABLE_EXCEPTION_SELFTEST
    asm volatile("ud2");
#endif

    shell_init();
    serial_puts("SHELL_READY\n");
    vga_puts("Shell ready. Type 'help' for commands.\n");
    shell_run();
}
