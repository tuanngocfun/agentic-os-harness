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

static void user_task(void) {
    while (1) {
        asm volatile("int $0x80" : : "a"(1), "b"((uint32_t)"U"));
        for (volatile int i = 0; i < 1000000; i++);
    }
}

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

    process_init();
    scheduler_init();

    struct process *p1 = process_create((uint32_t)user_task, 0);
    if (p1) {
        scheduler_add(p1);
    }

    shell_init();
    serial_puts("SHELL_READY\n");
    vga_puts("Shell ready. Type 'help' for commands.\n");
    shell_run();
}
