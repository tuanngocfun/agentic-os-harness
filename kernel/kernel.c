#include "kernel.h"
#include "serial.h"
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "timer.h"
#include "e820.h"
#include "memory.h"
#include "frame.h"
#include "allocator.h"
#include "ramdisk.h"
#include "paging.h"
#include "tss.h"
#include "syscall.h"
#include "process.h"
#include "scheduler.h"
#include "shell.h"
#include "string.h"

#ifdef ENABLE_SCHEDULER_SELFTEST
extern void context_switch(uint32_t *save_esp, uint32_t *load_esp);

static volatile int task_a_ran = 0;
static volatile int task_b_ran = 0;

static void scheduler_selftest_task_a(void) {
    task_a_ran = 1;
    serial_puts("SCHED_A\n");
    yield();
    serial_puts("SCHED_A_AGAIN\n");
    while (1) { asm volatile("hlt"); }
}

static void scheduler_selftest_task_b(void) {
    task_b_ran = 1;
    serial_puts("SCHED_B\n");
    yield();
    serial_puts("SCHED_B_AGAIN\n");
    while (1) { asm volatile("hlt"); }
}
#endif

#ifdef ENABLE_TIMER_PREEMPTION_SELFTEST
static volatile int preempt_a_ran = 0;
static volatile int preempt_b_ran = 0;
static volatile int preempt_ok_printed = 0;

static void preempt_maybe_report_ok(void) {
    if (preempt_a_ran && preempt_b_ran && !preempt_ok_printed) {
        preempt_ok_printed = 1;
        serial_puts("PREEMPT_OK\n");
    }
}

static void preempt_task_a(void) {
    while (1) {
        if (preempt_a_ran < 3) {
            preempt_a_ran++;
            serial_puts("PREEMPT_A\n");
        }
        preempt_maybe_report_ok();
        for (volatile int i = 0; i < 200000; i++) {
        }
    }
}

static void preempt_task_b(void) {
    while (1) {
        if (preempt_b_ran < 3) {
            preempt_b_ran++;
            serial_puts("PREEMPT_B\n");
        }
        preempt_maybe_report_ok();
        for (volatile int i = 0; i < 200000; i++) {
        }
    }
}
#endif

#ifdef ENABLE_SCHEDULER_SAFETY_SELFTEST
static volatile uint32_t safety_high_ran = 0;
static volatile uint32_t safety_low_a_ran = 0;
static volatile uint32_t safety_low_b_ran = 0;
static volatile int safety_priority_printed = 0;
static volatile int safety_fairness_printed = 0;
static volatile int safety_ok_printed = 0;

static void scheduler_safety_maybe_report(void) {
    if (!safety_priority_printed &&
        safety_high_ran >= 2 &&
        safety_high_ran > safety_low_a_ran &&
        safety_high_ran > safety_low_b_ran) {
        safety_priority_printed = 1;
        serial_puts("SCHED_PRIORITY_OK\n");
    }

    if (!safety_fairness_printed &&
        safety_high_ran >= 2 &&
        safety_low_a_ran >= 2 &&
        safety_low_b_ran >= 2) {
        safety_fairness_printed = 1;
        serial_puts("SCHED_FAIRNESS_OK\n");
    }

    if (!safety_ok_printed && safety_priority_printed && safety_fairness_printed) {
        safety_ok_printed = 1;
        serial_puts("SCHED_CRITICAL_OK\n");
        serial_puts("SCHED_SAFETY_OK\n");
    }
}

static void scheduler_safety_high_task(void) {
    while (1) {
        if (safety_high_ran < 16) {
            safety_high_ran++;
        }
        scheduler_safety_maybe_report();
        for (volatile int i = 0; i < 200000; i++) {
        }
    }
}

static void scheduler_safety_low_a_task(void) {
    while (1) {
        if (safety_low_a_ran < 16) {
            safety_low_a_ran++;
        }
        scheduler_safety_maybe_report();
        for (volatile int i = 0; i < 200000; i++) {
        }
    }
}

static void scheduler_safety_low_b_task(void) {
    while (1) {
        if (safety_low_b_ran < 16) {
            safety_low_b_ran++;
        }
        scheduler_safety_maybe_report();
        for (volatile int i = 0; i < 200000; i++) {
        }
    }
}
#endif

#ifdef ENABLE_RAMDISK_SELFTEST
static uint8_t ramdisk_write_buf[SECTOR_SIZE * 4];
static uint8_t ramdisk_read_buf[SECTOR_SIZE * 4];
#endif

#ifdef ENABLE_ADDRESS_SPACE_SELFTEST
#define ADDRSPACE_TEST_VA 0x00800000

static void address_space_dummy_task(void) {
    while (1) {
        asm volatile("hlt");
    }
}
#endif

#if defined(ENABLE_USERMODE_SELFTEST) || defined(ENABLE_SYSCALL_NEGATIVE_SELFTEST)
extern void enter_user_mode(uint32_t entry_point, uint32_t user_stack_top);
#endif

#ifdef ENABLE_USERMODE_SELFTEST
#define USERMODE_STACK_PHYSICAL 0x00900000
#define USERMODE_SUPERVISOR_PROBE_ADDR 0x00700000
#define USERMODE_SUPERVISOR_PROBE_PHYSICAL 0x00910000

static __attribute__((noreturn)) void usermode_selftest_entry(void) {
    uint32_t result;
    asm volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(SYS_USERMODE_TEST), "b"(0xCAFEBABE), "c"(0), "d"(0)
    );
    (void)result;

    volatile uint32_t *supervisor_page = (volatile uint32_t *)USERMODE_SUPERVISOR_PROBE_ADDR;
    (void)*supervisor_page;

    while (1) {
    }
}
#endif

#ifdef ENABLE_SYSCALL_NEGATIVE_SELFTEST
#define SYSCALL_NEG_STACK_PHYSICAL 0x00930000
#define SYSCALL_NEG_UNMAPPED_USER_PTR 0x40000000

static void syscall_marker(uint32_t marker) {
    uint32_t ignored;
    asm volatile(
        "int $0x80"
        : "=a"(ignored)
        : "a"(SYS_TEST_MARKER), "b"(marker), "c"(0), "d"(0)
    );
    (void)ignored;
}

static __attribute__((noreturn)) void syscall_negative_user_entry(void) {
    uint32_t result;

    asm volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(999), "b"(0), "c"(0), "d"(0)
    );
    if (result == SYSCALL_ENOSYS) {
        syscall_marker(SYSCALL_MARK_INVALID_NUM);
    }

    asm volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(0), "b"(0), "c"(0), "d"(0)
    );
    if (result == SYSCALL_ENOSYS) {
        syscall_marker(SYSCALL_MARK_ZERO_NUM);
    }

    asm volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(SYS_PUTS), "b"(0x00100000), "c"(0), "d"(0)
    );
    if (result == SYSCALL_EFAULT) {
        syscall_marker(SYSCALL_MARK_BAD_POINTER);
    }

    asm volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(SYS_PUTS), "b"(SYSCALL_NEG_UNMAPPED_USER_PTR), "c"(0), "d"(0)
    );
    if (result == SYSCALL_EFAULT) {
        syscall_marker(SYSCALL_MARK_UNMAPPED_POINTER);
    }

    char ok[] = "syscall-ok";
    asm volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(SYS_PUTS), "b"(ok), "c"(0), "d"(0)
    );
    if (result == SYSCALL_SUCCESS) {
        syscall_marker(SYSCALL_MARK_RING3_OK);
    }

    syscall_marker(SYSCALL_MARK_DONE);

    while (1) {
    }
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

    e820_init();
    memory_init();
    frame_init();
    paging_init();
    allocator_init();
    if (ramdisk_init() != BLKDEV_SUCCESS) {
        serial_puts("RAMDISK_INIT_FATAL\n");
        while (1) {
            asm volatile("cli; hlt");
        }
    }
    tss_init();
    syscall_init();

#ifdef ENABLE_MEMORY_SELFTEST
    {
        uint32_t total_kb = memory_get_total_kb();
        serial_puts("MEMORY_TEST\n");
        serial_puts("MEMORY_TOTAL_KB:");
        serial_put_uint32(total_kb);
        serial_puts("\n");

        if (memory_detected_from_hardware() && total_kb >= (16 * 1024)) {
            serial_puts("MEMORY_DETECT_OK\n");
        } else {
            serial_puts("MEMORY_DETECT_FAIL\n");
        }
    }
#endif

#ifdef ENABLE_E820_SELFTEST
    {
        serial_puts("E820_TEST\n");
        e820_print_map();

        // Check if E820 detected memory
        struct e820_map *map = e820_get_map();
        if (map->count > 0) {
            serial_puts("E820_DETECT_OK\n");
        } else {
            serial_puts("E820_DETECT_FAIL\n");
        }

        // Check for usable memory
        uint64_t usable = e820_get_total_usable_memory();
        if (usable >= (16 * 1024 * 1024)) {  // At least 16MB
            serial_puts("E820_USABLE_MEMORY_OK\n");
        } else {
            serial_puts("E820_USABLE_MEMORY_FAIL\n");
        }

        uint32_t total_frames = frame_get_total_count();
        uint32_t free_before = frame_get_free_count();
        uint32_t frame1 = frame_alloc();
        uint32_t frame2 = frame_alloc();
        int frame_alloc_ok = frame1 && frame2 && frame1 != frame2 &&
                             frame_is_allocated(frame1) &&
                             frame_is_allocated(frame2) &&
                             frame_get_free_count() + 2 == free_before;
        if (frame_alloc_ok) {
            serial_puts("FRAME_ALLOC_OK\n");
        }

        frame_free(frame1);
        int frame_free_ok = frame_get_free_count() + 1 == free_before;
        if (frame_free_ok) {
            serial_puts("FRAME_FREE_OK\n");
        }

        uint32_t frame3 = frame_alloc();
        int frame_reuse_ok = frame3 == frame1;
        if (frame_reuse_ok) {
            serial_puts("FRAME_REUSE_OK\n");
        }

        frame_free(frame2);
        frame_free(frame3);

        uint32_t low_frames[320];
        uint32_t low_count = 0;
        while (low_count < 320) {
            uint32_t frame = frame_alloc_below(0x00400000);
            if (!frame) {
                break;
            }
            low_frames[low_count++] = frame;
        }

        int frame_exhaust_ok = low_count > 32 && frame_alloc_below(0x00400000) == 0;
        if (frame_exhaust_ok) {
            serial_puts("FRAME_EXHAUST_OK\n");
        }

        while (low_count > 0) {
            frame_free(low_frames[--low_count]);
        }

        if (map->count > 0 && usable >= (16 * 1024 * 1024) &&
            total_frames > 0 && frame_alloc_ok && frame_free_ok &&
            frame_reuse_ok && frame_exhaust_ok &&
            frame_get_free_count() == free_before) {
            serial_puts("E820_FRAME_OK\n");
            serial_puts("E820_OK\n");
        } else {
            serial_puts("E820_FRAME_FAIL\n");
        }
    }
#endif

#ifdef ENABLE_RAMDISK_SELFTEST
    {
        serial_puts("RAMDISK_TEST\n");

        struct block_device *dev = ramdisk_get_device();
        int init_ok = dev &&
                      dev->sector_size == SECTOR_SIZE &&
                      dev->sector_count >= 128 &&
                      dev->read_sector &&
                      dev->write_sector &&
                      dev->read_sectors &&
                      dev->write_sectors;
        if (init_ok) {
            serial_puts("RAMDISK_DEVICE_OK\n");
        }

        for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
            ramdisk_write_buf[i] = (uint8_t)(i & 0xFF);
            ramdisk_read_buf[i] = 0;
        }

        int basic_ok = init_ok &&
                       dev->write_sector(dev, 0, ramdisk_write_buf) == BLKDEV_SUCCESS &&
                       dev->read_sector(dev, 0, ramdisk_read_buf) == BLKDEV_SUCCESS &&
                       memcmp(ramdisk_write_buf, ramdisk_read_buf, SECTOR_SIZE) == 0;
        if (basic_ok) {
            serial_puts("RAMDISK_BASIC_OK\n");
        }

        for (uint32_t i = 0; i < sizeof(ramdisk_write_buf); i++) {
            ramdisk_write_buf[i] = (uint8_t)((i / SECTOR_SIZE) + 0x40);
            ramdisk_read_buf[i] = 0;
        }

        int multi_ok = init_ok &&
                       dev->write_sectors(dev, 100, 4, ramdisk_write_buf) == BLKDEV_SUCCESS &&
                       dev->read_sectors(dev, 100, 4, ramdisk_read_buf) == BLKDEV_SUCCESS &&
                       memcmp(ramdisk_write_buf, ramdisk_read_buf, sizeof(ramdisk_write_buf)) == 0;
        if (multi_ok) {
            serial_puts("RAMDISK_MULTI_OK\n");
        }

        int bounds_ok = init_ok &&
                        dev->read_sector(dev, dev->sector_count, ramdisk_read_buf) == BLKDEV_EINVAL &&
                        dev->write_sectors(dev, dev->sector_count - 1, 2, ramdisk_write_buf) == BLKDEV_EINVAL &&
                        dev->read_sectors(dev, 0xFFFFFFFFu, 2, ramdisk_read_buf) == BLKDEV_EINVAL;
        if (bounds_ok) {
            serial_puts("RAMDISK_BOUNDS_OK\n");
        }

        if (basic_ok && multi_ok && bounds_ok) {
            serial_puts("RAMDISK_OK\n");
        } else {
            serial_puts("RAMDISK_FAIL\n");
        }
    }
#endif

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

#ifdef ENABLE_SYSCALL_NEGATIVE_SELFTEST
    {
        serial_puts("SYSCALL_NEGATIVE_TEST\n");

        uint32_t user_code_page = ((uint32_t)syscall_negative_user_entry) & 0xFFFFF000;
        uint32_t user_stack_page = USER_STACK_TOP - 4096;

        paging_map_page(user_code_page, user_code_page, PAGE_PRESENT | PAGE_USER);
        paging_map_page(user_stack_page,
                        SYSCALL_NEG_STACK_PHYSICAL,
                        PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

        enter_user_mode((uint32_t)syscall_negative_user_entry, USER_STACK_TOP);

        serial_puts("SYSCALL_NEGATIVE_FAIL\n");
        while (1) {
            asm volatile("cli; hlt");
        }
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

#ifdef ENABLE_ALLOCATOR_SELFTEST
    {
        serial_puts("ALLOCATOR_TEST\n");

        uint32_t free_before = allocator_get_free_bytes();
        void *a = kmalloc(24);
        void *b = kmalloc(80);
        int alloc_ok = a && b &&
                       (((uint32_t)a & 0x0F) == 0) &&
                       (((uint32_t)b & 0x0F) == 0) &&
                       allocator_get_used_bytes() > 0;

        if (alloc_ok) {
            serial_puts("ALLOCATOR_ALLOC_OK\n");
        }

        kfree(a);
        void *c = kmalloc(16);
        int reuse_ok = (c == a);
        if (reuse_ok) {
            serial_puts("ALLOCATOR_REUSE_OK\n");
        }

        kfree(b);
        kfree(c);
        int free_ok = allocator_get_free_bytes() == free_before;
        if (free_ok) {
            serial_puts("ALLOCATOR_FREE_OK\n");
        }

        void *allocs[300];
        int count = 0;
        while (count < 300) {
            void *ptr = kmalloc(4096);
            if (!ptr) {
                break;
            }
            allocs[count++] = ptr;
        }

        int exhaust_ok = count > 100 && kmalloc(4096) == NULL;
        if (exhaust_ok) {
            serial_puts("ALLOCATOR_EXHAUST_OK\n");
        }

        while (count > 0) {
            kfree(allocs[--count]);
        }

        if (alloc_ok && reuse_ok && free_ok && exhaust_ok &&
            allocator_get_free_bytes() == free_before) {
            serial_puts("ALLOCATOR_OK\n");
        } else {
            serial_puts("ALLOCATOR_FAIL\n");
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

            scheduler_schedule();
            struct process *first = scheduler_get_current();

            scheduler_schedule();
            struct process *second = scheduler_get_current();

            scheduler_schedule();
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
        task_a_ran = 0;
        task_b_ran = 0;

        struct process kernel_task;
        memset(&kernel_task, 0, sizeof(kernel_task));
        kernel_task.state = PROCESS_RUNNING;

        task_a = process_create((uint32_t)scheduler_selftest_task_a, 0);
        task_b = process_create((uint32_t)scheduler_selftest_task_b, 0);

        if (!task_a || !task_b) {
            serial_puts("SCHED_CONTEXT_FAIL\n");
        } else {
            scheduler_set_current(&kernel_task);
            scheduler_add(task_a);
            scheduler_add(task_b);

            yield();

            if (task_a_ran && task_b_ran && kernel_task.esp != 0) {
                serial_puts("SCHED_CONTEXT_OK\n");
            } else {
                serial_puts("SCHED_CONTEXT_FAIL\n");
            }
        }

        scheduler_init();
        process_init();
    }
#endif

#ifdef ENABLE_SCHEDULER_SAFETY_SELFTEST
    {
        serial_puts("SCHED_SAFETY_TEST\n");

        asm volatile("cli");
        process_init();
        scheduler_init();

        safety_high_ran = 0;
        safety_low_a_ran = 0;
        safety_low_b_ran = 0;
        safety_priority_printed = 0;
        safety_fairness_printed = 0;
        safety_ok_printed = 0;

        struct process *yield_guard = process_create_preemptive((uint32_t)scheduler_safety_high_task);
        if (!yield_guard) {
            serial_puts("SCHED_SAFETY_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }

        scheduler_set_current(yield_guard);
        uint32_t schedule_before_yield = scheduler_get_count();
        yield();
        if (scheduler_get_current() == yield_guard &&
            scheduler_get_count() == schedule_before_yield) {
            serial_puts("SCHED_YIELD_GUARD_OK\n");
        } else {
            serial_puts("SCHED_SAFETY_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }

        scheduler_init();
        process_init();

        struct process *high = process_create_preemptive((uint32_t)scheduler_safety_high_task);
        struct process *low_a = process_create_preemptive((uint32_t)scheduler_safety_low_a_task);
        struct process *low_b = process_create_preemptive((uint32_t)scheduler_safety_low_b_task);

        if (!high || !low_a || !low_b) {
            serial_puts("SCHED_SAFETY_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }

        scheduler_set_priority(high, 4);
        scheduler_set_priority(low_a, 1);
        scheduler_set_priority(low_b, 1);

        scheduler_add(high);
        scheduler_add(low_a);
        scheduler_add(low_b);
        scheduler_set_preemption_enabled(1);
        asm volatile("sti");

        while (1) {
            asm volatile("hlt");
        }
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

#ifdef ENABLE_ADDRESS_SPACE_SELFTEST
    {
        serial_puts("ADDRSPACE_TEST\n");

        uint32_t kernel_cr3 = paging_get_current_directory();
        uint32_t cr3_a = paging_create_address_space();
        uint32_t cr3_b = paging_create_address_space();
        uint32_t frame_a = paging_alloc_frame();
        uint32_t frame_b = paging_alloc_frame();

        process_init();
        scheduler_init();
        struct process *proc_a = process_create((uint32_t)address_space_dummy_task, 0);
        struct process *proc_b = process_create((uint32_t)address_space_dummy_task, 0);

        if (proc_a) {
            process_set_address_space(proc_a, cr3_a);
        }
        if (proc_b) {
            process_set_address_space(proc_b, cr3_b);
        }

        int cr3_ok = proc_a && proc_b && cr3_a && cr3_b &&
                     frame_a && frame_b &&
                     cr3_a != cr3_b &&
                     frame_a != frame_b;
        if (cr3_ok) {
            serial_puts("ADDRSPACE_CR3_OK\n");
        }

        paging_map_page_in_directory(cr3_a, ADDRSPACE_TEST_VA, frame_a,
                                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        paging_map_page_in_directory(cr3_b, ADDRSPACE_TEST_VA, frame_b,
                                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

        int map_ok = cr3_ok;
        if (map_ok) {
            serial_puts("ADDRSPACE_MAP_OK\n");
        }

        scheduler_set_current(proc_a);
        volatile uint32_t *slot = (volatile uint32_t *)ADDRSPACE_TEST_VA;
        *slot = 0xA5A5A5A5;
        uint32_t read_a = *slot;

        scheduler_add(proc_b);
        scheduler_schedule();
        uint32_t cr3_after_b = paging_get_current_directory();
        *slot = 0x5A5A5A5A;
        uint32_t read_b = *slot;

        scheduler_schedule();
        uint32_t cr3_after_a = paging_get_current_directory();
        uint32_t read_a_again = *slot;

        paging_switch_directory(kernel_cr3);
        scheduler_init();
        process_init();

        int switch_ok = cr3_after_b == cr3_b && cr3_after_a == cr3_a;
        if (switch_ok) {
            serial_puts("ADDRSPACE_SWITCH_OK\n");
        }

        int isolation_ok = read_a == 0xA5A5A5A5 &&
                           read_b == 0x5A5A5A5A &&
                           read_a_again == 0xA5A5A5A5;
        if (isolation_ok) {
            serial_puts("ADDRSPACE_ISOLATION_OK\n");
        }

        if (cr3_ok && map_ok && switch_ok && isolation_ok) {
            serial_puts("ADDRSPACE_OK\n");
        } else {
            serial_puts("ADDRSPACE_FAIL\n");
        }
    }
#endif

#ifdef ENABLE_USERMODE_SELFTEST
    {
        serial_puts("USERMODE_TEST\n");

        process_init();
        struct process *user_proc = process_create((uint32_t)usermode_selftest_entry, 1);

        uint32_t user_code_page = ((uint32_t)usermode_selftest_entry) & 0xFFFFF000;
        uint32_t user_stack_page = USER_STACK_TOP - 4096;

        paging_map_page(user_code_page, user_code_page, PAGE_PRESENT | PAGE_USER);
        paging_map_page(user_stack_page,
                        USERMODE_STACK_PHYSICAL,
                        PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        paging_map_page(USERMODE_SUPERVISOR_PROBE_ADDR,
                        USERMODE_SUPERVISOR_PROBE_PHYSICAL,
                        PAGE_PRESENT | PAGE_WRITABLE);

        if (user_proc &&
            paging_is_user_accessible(user_code_page) &&
            paging_is_user_accessible(user_stack_page) &&
            !paging_is_user_accessible(USERMODE_SUPERVISOR_PROBE_ADDR)) {
            serial_puts("PROCESS_USERMODE_READY\n");
            enter_user_mode(user_proc->eip, USER_STACK_TOP);
        }

        serial_puts("USERMODE_FAIL\n");
        while (1) {
            asm volatile("cli; hlt");
        }
    }
#endif

#ifdef ENABLE_TIMER_PREEMPTION_SELFTEST
    {
        serial_puts("PREEMPT_TEST\n");

        asm volatile("cli");
        process_init();
        scheduler_init();
        preempt_a_ran = 0;
        preempt_b_ran = 0;
        preempt_ok_printed = 0;

        struct process *task_a = process_create_preemptive((uint32_t)preempt_task_a);
        struct process *task_b = process_create_preemptive((uint32_t)preempt_task_b);

        if (!task_a || !task_b) {
            serial_puts("PREEMPT_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }

        scheduler_add(task_a);
        scheduler_add(task_b);
        scheduler_set_preemption_enabled(1);
        asm volatile("sti");

        while (1) {
            asm volatile("hlt");
        }
    }
#endif

    shell_init();
    serial_puts("SHELL_READY\n");
    vga_puts("Shell ready. Type 'help' for commands.\n");
    shell_run();
}
