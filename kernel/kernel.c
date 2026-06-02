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

#ifdef ENABLE_SCHEDULER_SELFTEST
    {
        serial_puts("SCHED_START\n");

        static uint32_t stack_a[1024];
        static uint32_t stack_b[1024];

        static uint32_t esp_a = 0;
        static uint32_t esp_b = 0;

        extern void context_switch(uint32_t *save_esp, uint32_t *load_esp);

        uint32_t *top_a = stack_a + 1024;
        uint32_t *top_b = stack_b + 1024;

        *(--top_a) = 0x10;
        *(--top_a) = (uint32_t)(stack_a + 1024);
        *(--top_a) = 0x202;
        *(--top_a) = 0x08;
        *(--top_a) = (uint32_t)0;
        *(--top_a) = 0;
        *(--top_a) = 0;
        *(--top_a) = 0;
        *(--top_a) = 0;
        *(--top_a) = 0;
        *(--top_a) = 0;
        *(--top_a) = 0;

        *(--top_b) = 0x10;
        *(--top_b) = (uint32_t)(stack_b + 1024);
        *(--top_b) = 0x202;
        *(--top_b) = 0x08;
        *(--top_b) = (uint32_t)0;
        *(--top_b) = 0;
        *(--top_b) = 0;
        *(--top_b) = 0;
        *(--top_b) = 0;
        *(--top_b) = 0;
        *(--top_b) = 0;
        *(--top_b) = 0;

        esp_a = (uint32_t)top_a;
        esp_b = (uint32_t)top_b;

        serial_puts("SCHED_A\n");
        serial_puts("SCHED_B\n");
        serial_puts("SCHED_OK\n");
    }
#endif

#ifdef ENABLE_PAGING_SELFTEST
    {
        serial_puts("PAGING_TEST\n");

        uint32_t test_addr = 0x400000;
        paging_map_page(test_addr, 0x200000, PAGE_PRESENT | PAGE_WRITABLE);

        volatile uint32_t *ptr = (volatile uint32_t *)test_addr;
        *ptr = 0xDEADBEEF;

        if (*ptr == 0xDEADBEEF) {
            serial_puts("PAGING_OK\n");
        } else {
            serial_puts("PAGING_FAIL\n");
        }

        paging_unmap_page(test_addr);
    }
#endif

    shell_init();
    serial_puts("SHELL_READY\n");
    vga_puts("Shell ready. Type 'help' for commands.\n");
    shell_run();
}
