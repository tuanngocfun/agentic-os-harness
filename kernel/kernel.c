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
static void scheduler_selftest_task_a(void) {}
static void scheduler_selftest_task_b(void) {}
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

#ifdef ENABLE_TIMER_SELFTEST
    {
        serial_puts("TIMER_TEST\n");

        uint32_t ticks_before = timer_get_ticks();
        for (volatile int i = 0; i < 10000000; i++);
        uint32_t ticks_after = timer_get_ticks();

        if (ticks_after > ticks_before) {
            serial_puts("TIMER_TICKS_OK\n");
        } else {
            serial_puts("TIMER_TICKS_FAIL\n");
        }
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

        uint32_t perm_addr = 0x500000;
        paging_map_page(perm_addr, 0x300000, PAGE_PRESENT);
        int perm_ok = paging_is_mapped(perm_addr);
        if (perm_ok) {
            serial_puts("PAGING_PERM_OK\n");
        }

        page_fault_caught = 0;
        page_fault_addr = 0;
        paging_map_page(0x600000, 0x400000, PAGE_PRESENT);
        volatile uint32_t *ro_ptr = (volatile uint32_t *)0x600000;
        (void)*ro_ptr;
        int read_ok = (page_fault_caught == 0);
        if (read_ok) {
            serial_puts("PAGING_READ_OK\n");
        }

        page_fault_caught = 0;
        page_fault_addr = 0;
        page_fault_repair_virtual = 0x600000;
        page_fault_repair_physical = 0x400000;
        page_fault_repair_flags = PAGE_PRESENT | PAGE_WRITABLE;
        page_fault_expected = 1;
        *ro_ptr = 0x12345678;
        page_fault_expected = 0;
        int write_fault_ok = (page_fault_caught == 1 &&
                              page_fault_addr == 0x600000 &&
                              *ro_ptr == 0x12345678);
        if (write_fault_ok) {
            serial_puts("PAGING_WRITE_FAULT_OK\n");
        }
        page_fault_caught = 0;

        page_fault_caught = 0;
        page_fault_addr = 0;
        paging_unmap_page(0x600000);
        page_fault_repair_virtual = 0x600000;
        page_fault_repair_physical = 0x400000;
        page_fault_repair_flags = PAGE_PRESENT | PAGE_WRITABLE;
        page_fault_expected = 1;
        volatile uint32_t unmap_value = *ro_ptr;
        (void)unmap_value;
        page_fault_expected = 0;
        int unmap_fault_ok = (page_fault_caught == 1 && page_fault_addr == 0x600000);
        if (unmap_fault_ok) {
            serial_puts("PAGING_UNMAP_FAULT_OK\n");
        }
        page_fault_caught = 0;

        if (map_ok && unmap_ok && perm_ok && read_ok && write_fault_ok && unmap_fault_ok) {
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
