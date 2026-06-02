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

#ifdef ENABLE_SCHEDULER_SELFTEST
static volatile int sched_tick_count = 0;

static void scheduler_selftest_task_a(void) {
    serial_puts("SCHED_A\n");
}

static void scheduler_selftest_task_b(void) {
    serial_puts("SCHED_B\n");
}
#endif

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

#ifdef ENABLE_EXCEPTION_DIV0_SELFTEST
    asm volatile("xor %%eax,%%eax\n\tdiv %%eax" ::: "eax");
#endif

#ifdef ENABLE_EXCEPTION_GPF_SELFTEST
    asm volatile(
        "mov $0x28, %%ax\n\t"
        "mov %%ax, %%ds"
        :
        :
        : "ax"
    );
#endif

#ifdef ENABLE_EXCEPTION_PAGEFAULT_SELFTEST
    {
        volatile uint32_t *ptr = (volatile uint32_t *)0xDEAD0000;
        (void)*ptr;
    }
#endif

#ifdef ENABLE_SCHEDULER_SELFTEST
    {
        serial_puts("SCHED_START\n");

        process_init();
        scheduler_init();

        struct process *task_a = process_create((uint32_t)scheduler_selftest_task_a, 0);
        struct process *task_b = process_create((uint32_t)scheduler_selftest_task_b, 0);

        if (!task_a || !task_b) {
            serial_puts("SCHED_QUEUE_FAIL\n");
        } else {
            scheduler_add(task_a);
            scheduler_add(task_b);

            scheduler_tick();
            struct process *first = scheduler_get_current();

            scheduler_tick();
            struct process *second = scheduler_get_current();

            scheduler_tick();
            struct process *third = scheduler_get_current();

            if (first == task_a &&
                second == task_b &&
                third == task_a &&
                scheduler_get_count() == 3 &&
                process_get_count() == 2) {
                serial_puts("SCHED_QUEUE_OK\n");
            } else {
                serial_puts("SCHED_QUEUE_FAIL\n");
            }

            scheduler_selftest_task_a();
            scheduler_selftest_task_b();

            serial_puts("SCHED_OK\n");
        }

        scheduler_init();
        process_init();
    }
#endif

#ifdef ENABLE_PAGING_SELFTEST
    {
        serial_puts("PAGING_TEST\n");

        uint32_t test_addr = 0x400000;
        paging_map_page(test_addr, 0x200000, PAGE_PRESENT | PAGE_WRITABLE);

        volatile uint32_t *ptr = (volatile uint32_t *)test_addr;
        *ptr = 0xDEADBEEF;

        int map_ok = paging_is_mapped(test_addr) && (*ptr == 0xDEADBEEF);
        if (map_ok) {
            serial_puts("PAGING_MAP_OK\n");
        }

        paging_unmap_page(test_addr);
        int unmap_ok = !paging_is_mapped(test_addr);
        if (unmap_ok) {
            serial_puts("PAGING_UNMAP_OK\n");
        }

        if (map_ok && unmap_ok) {
            serial_puts("PAGING_OK\n");
        } else {
            serial_puts("PAGING_FAIL\n");
        }
    }
#endif

    shell_init();
    serial_puts("SHELL_READY\n");
    vga_puts("Shell ready. Type 'help' for commands.\n");
    shell_run();
}
