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
#include "vfs.h"
#include "elf.h"
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

#ifdef ENABLE_VFS_SELFTEST
static uint8_t vfs_write_buf[SECTOR_SIZE * 2 + 37];
static uint8_t vfs_read_buf[SECTOR_SIZE * 2 + 37];
#endif

#if defined(ENABLE_ELF_LOADER_SELFTEST) || defined(ENABLE_PROCESS_SYSCALL_SELFTEST) || \
    defined(ENABLE_REDTEAM_SELFTEST)
#define ELF_TEST_VADDR 0x40000000u
#define ELF_TEST_ENTRY (ELF_TEST_VADDR + 0x20u)
#define ELF_TEST_FILE_SIZE 512u
#define ELF_TEST_SEGMENT_OFFSET 128u
#define ELF_TEST_SEGMENT_FILE_SIZE 9u
#define ELF_TEST_SEGMENT_MEM_SIZE 64u

static uint8_t elf_test_image[ELF_TEST_FILE_SIZE];
#endif

#ifdef ENABLE_ELF_LOADER_SELFTEST
static uint8_t elf_bad_image[ELF_TEST_FILE_SIZE];
static uint8_t elf_trunc_image[32];
static uint8_t elf_kernel_addr_image[ELF_TEST_FILE_SIZE];
#endif

#if defined(ENABLE_ELF_LOADER_SELFTEST) || defined(ENABLE_PROCESS_SYSCALL_SELFTEST) || \
    defined(ENABLE_REDTEAM_SELFTEST)
static void elf_put16(uint8_t *buf, uint32_t offset, uint16_t value) {
    buf[offset] = (uint8_t)(value & 0xFF);
    buf[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
}

static void elf_put32(uint8_t *buf, uint32_t offset, uint32_t value) {
    buf[offset] = (uint8_t)(value & 0xFF);
    buf[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    buf[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    buf[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static void elf_make_image(uint8_t *buf, uint32_t vaddr) {
    memset(buf, 0, ELF_TEST_FILE_SIZE);

    buf[0] = 0x7F;
    buf[1] = 'E';
    buf[2] = 'L';
    buf[3] = 'F';
    buf[4] = 1;
    buf[5] = 1;
    buf[6] = 1;

    elf_put16(buf, 16, 2);
    elf_put16(buf, 18, 3);
    elf_put32(buf, 20, 1);
    elf_put32(buf, 24, vaddr + 0x20);
    elf_put32(buf, 28, 52);
    elf_put32(buf, 32, 0);
    elf_put32(buf, 36, 0);
    elf_put16(buf, 40, 52);
    elf_put16(buf, 42, 32);
    elf_put16(buf, 44, 1);
    elf_put16(buf, 46, 0);
    elf_put16(buf, 48, 0);
    elf_put16(buf, 50, 0);

    elf_put32(buf, 52, 1);
    elf_put32(buf, 56, ELF_TEST_SEGMENT_OFFSET);
    elf_put32(buf, 60, vaddr);
    elf_put32(buf, 64, vaddr);
    elf_put32(buf, 68, ELF_TEST_SEGMENT_FILE_SIZE);
    elf_put32(buf, 72, ELF_TEST_SEGMENT_MEM_SIZE);
    elf_put32(buf, 76, 5);
    elf_put32(buf, 80, 1);

    buf[ELF_TEST_SEGMENT_OFFSET + 0] = 'E';
    buf[ELF_TEST_SEGMENT_OFFSET + 1] = 'L';
    buf[ELF_TEST_SEGMENT_OFFSET + 2] = 'F';
    buf[ELF_TEST_SEGMENT_OFFSET + 3] = 'P';
    buf[ELF_TEST_SEGMENT_OFFSET + 4] = 'R';
    buf[ELF_TEST_SEGMENT_OFFSET + 5] = 'E';
    buf[ELF_TEST_SEGMENT_OFFSET + 6] = 'P';
    buf[ELF_TEST_SEGMENT_OFFSET + 7] = 'O';
    buf[ELF_TEST_SEGMENT_OFFSET + 8] = 'K';
}

static int elf_write_fixture(const char *path, const uint8_t *buf, uint32_t size) {
    int fd = vfs_open(path, VFS_O_CREAT | VFS_O_RDWR | VFS_O_TRUNC);
    int status;

    if (fd < 0) {
        return 0;
    }

    status = vfs_write(fd, buf, size);
    if (vfs_close(fd) != VFS_SUCCESS) {
        return 0;
    }

    return status == (int)size;
}
#endif

#ifdef ENABLE_ELF_LOADER_SELFTEST
static int elf_loaded_segment_ok(void) {
    const uint8_t *loaded = (const uint8_t *)ELF_TEST_VADDR;
    const uint8_t *expected = elf_test_image + ELF_TEST_SEGMENT_OFFSET;

    if (memcmp(loaded, expected, ELF_TEST_SEGMENT_FILE_SIZE) != 0) {
        return 0;
    }

    for (uint32_t i = ELF_TEST_SEGMENT_FILE_SIZE; i < ELF_TEST_SEGMENT_MEM_SIZE; i++) {
        if (loaded[i] != 0) {
            return 0;
        }
    }

    return 1;
}
#endif

#ifdef ENABLE_ADDRESS_SPACE_SELFTEST
#define ADDRSPACE_TEST_VA 0x00800000

static void address_space_dummy_task(void) {
    while (1) {
        asm volatile("hlt");
    }
}
#endif

#if defined(ENABLE_USERMODE_SELFTEST) || defined(ENABLE_SYSCALL_NEGATIVE_SELFTEST) || \
    defined(ENABLE_SYSCALL_FILE_SELFTEST) || defined(ENABLE_PROCESS_SYSCALL_SELFTEST) || \
    defined(ENABLE_REDTEAM_SELFTEST)
extern void enter_user_mode(uint32_t entry_point, uint32_t user_stack_top);
#endif

#ifdef ENABLE_USERMODE_SELFTEST
#define USERMODE_STACK_PHYSICAL 0x00900000
#define USERMODE_SUPERVISOR_PROBE_ADDR 0x00700000
#define USERMODE_SUPERVISOR_PROBE_PHYSICAL 0x00910000

static __attribute__((noreturn)) void usermode_selftest_entry(void) {
    uint32_t result = SYS_USERMODE_TEST;
    asm volatile(
        "int $0x80"
        : "+a"(result)
        : "b"(0xCAFEBABE), "c"(0), "d"(0)
        : "memory", "cc"
    );
    (void)result;

    volatile uint32_t *supervisor_page = (volatile uint32_t *)USERMODE_SUPERVISOR_PROBE_ADDR;
    (void)*supervisor_page;

    while (1) {
    }
}
#endif

#if defined(ENABLE_SYSCALL_NEGATIVE_SELFTEST) || defined(ENABLE_SYSCALL_FILE_SELFTEST) || \
    defined(ENABLE_PROCESS_SYSCALL_SELFTEST) || defined(ENABLE_REDTEAM_SELFTEST)
static void syscall_marker(uint32_t marker) {
    uint32_t ignored = SYS_TEST_MARKER;
    asm volatile(
        "int $0x80"
        : "+a"(ignored)
        : "b"(marker), "c"(SYSCALL_TEST_MARKER_TOKEN), "d"(0)
        : "memory", "cc"
    );
    (void)ignored;
}

#endif

#ifdef ENABLE_SYSCALL_NEGATIVE_SELFTEST
#define SYSCALL_NEG_STACK_PHYSICAL 0x00930000
#define SYSCALL_NEG_UNMAPPED_USER_PTR 0x40000000

static __attribute__((noreturn)) void syscall_negative_user_entry(void) {
    uint32_t result;

    result = 999;
    asm volatile(
        "int $0x80"
        : "+a"(result)
        : "b"(0), "c"(0), "d"(0)
        : "memory", "cc"
    );
    if (result == SYSCALL_ENOSYS) {
        syscall_marker(SYSCALL_MARK_INVALID_NUM);
    }

    result = 0;
    asm volatile(
        "int $0x80"
        : "+a"(result)
        : "b"(0), "c"(0), "d"(0)
        : "memory", "cc"
    );
    if (result == SYSCALL_ENOSYS) {
        syscall_marker(SYSCALL_MARK_ZERO_NUM);
    }

    result = SYS_PUTS;
    asm volatile(
        "int $0x80"
        : "+a"(result)
        : "b"(0x00100000), "c"(0), "d"(0)
        : "memory", "cc"
    );
    if (result == SYSCALL_EFAULT) {
        syscall_marker(SYSCALL_MARK_BAD_POINTER);
    }

    result = SYS_PUTS;
    asm volatile(
        "int $0x80"
        : "+a"(result)
        : "b"(SYSCALL_NEG_UNMAPPED_USER_PTR), "c"(0), "d"(0)
        : "memory", "cc"
    );
    if (result == SYSCALL_EFAULT) {
        syscall_marker(SYSCALL_MARK_UNMAPPED_POINTER);
    }

    char ok[] = "syscall-ok";
    result = SYS_PUTS;
    asm volatile(
        "int $0x80"
        : "+a"(result)
        : "b"(ok), "c"(0), "d"(0)
        : "memory", "cc"
    );
    if (result == SYSCALL_SUCCESS) {
        syscall_marker(SYSCALL_MARK_RING3_OK);
    }

    syscall_marker(SYSCALL_MARK_DONE);

    while (1) {
    }
}
#endif

#if defined(ENABLE_SYSCALL_FILE_SELFTEST) || defined(ENABLE_PROCESS_SYSCALL_SELFTEST) || \
    defined(ENABLE_REDTEAM_SELFTEST)
static uint32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    asm volatile(
        "int $0x80"
        : "+a"(num)
        : "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory", "cc"
    );
    return num;
}
#endif

#ifdef ENABLE_SYSCALL_FILE_SELFTEST
#define SYSCALL_FILE_STACK_PHYSICAL 0x00940000
#define SYSCALL_FILE_DATA_VIRTUAL 0xAFFF0000
#define SYSCALL_FILE_DATA_PHYSICAL 0x00941000
#define SYSCALL_FILE_BUFFER_SIZE 64

static void syscall_file_set_path(char *path) {
    path[0] = '/';
    path[1] = 's';
    path[2] = 'y';
    path[3] = 's';
    path[4] = 'f';
    path[5] = 'i';
    path[6] = 'l';
    path[7] = 'e';
    path[8] = '\0';
}

static void syscall_file_set_missing_path(char *path) {
    path[0] = '/';
    path[1] = 'm';
    path[2] = 'i';
    path[3] = 's';
    path[4] = 's';
    path[5] = 'i';
    path[6] = 'n';
    path[7] = 'g';
    path[8] = '\0';
}

static void syscall_file_set_nested_path(char *path) {
    path[0] = '/';
    path[1] = 'n';
    path[2] = 'e';
    path[3] = 's';
    path[4] = 't';
    path[5] = '/';
    path[6] = 'x';
    path[7] = '\0';
}

static __attribute__((noreturn)) void syscall_file_user_entry(void) {
    char *path = (char *)SYSCALL_FILE_DATA_VIRTUAL;
    char *missing_path = (char *)(SYSCALL_FILE_DATA_VIRTUAL + 16);
    char *nested_path = (char *)(SYSCALL_FILE_DATA_VIRTUAL + 32);
    uint8_t *write_buf = (uint8_t *)(SYSCALL_FILE_DATA_VIRTUAL + 64);
    uint8_t *read_buf = (uint8_t *)(SYSCALL_FILE_DATA_VIRTUAL + 128);
    struct syscall_file_stat *stat = (struct syscall_file_stat *)(SYSCALL_FILE_DATA_VIRTUAL + 192);
    uint32_t fd;
    uint32_t result;
    int open_ok;
    int write_ok;
    int read_ok;
    int stat_ok;
    int negative_ok;
    int data_ok = 1;

    syscall_file_set_path(path);
    syscall_file_set_missing_path(missing_path);
    syscall_file_set_nested_path(nested_path);

    for (uint32_t i = 0; i < SYSCALL_FILE_BUFFER_SIZE; i++) {
        write_buf[i] = (uint8_t)((i * 5 + 11) & 0xFF);
        read_buf[i] = 0;
    }

    fd = syscall3(SYS_OPEN, (uint32_t)path, SYS_O_CREAT | SYS_O_RDWR | SYS_O_TRUNC, 0);
    open_ok = fd < VFS_MAX_OPEN_FILES;
    if (open_ok) {
        syscall_marker(SYSCALL_MARK_FILE_OPEN);
    } else {
        syscall_marker(SYSCALL_MARK_FILE_OPEN_FAIL);
    }

    result = open_ok ? syscall3(SYS_WRITE, fd, (uint32_t)write_buf, SYSCALL_FILE_BUFFER_SIZE) : SYSCALL_EBADF;
    write_ok = result == SYSCALL_FILE_BUFFER_SIZE &&
               syscall3(SYS_CLOSE, fd, 0, 0) == SYSCALL_SUCCESS;
    if (write_ok) {
        syscall_marker(SYSCALL_MARK_FILE_WRITE);
    } else {
        syscall_marker(SYSCALL_MARK_FILE_WRITE_FAIL);
        if (open_ok) {
            syscall3(SYS_CLOSE, fd, 0, 0);
        }
    }

    fd = write_ok ? syscall3(SYS_OPEN, (uint32_t)path, SYS_O_RDONLY, 0) : SYSCALL_EBADF;
    if (fd >= VFS_MAX_OPEN_FILES) {
        data_ok = 0;
    }
    result = data_ok ? syscall3(SYS_READ, fd, (uint32_t)read_buf, 7) : SYSCALL_EBADF;
    if (result != 7) {
        data_ok = 0;
    }
    result = data_ok ? syscall3(SYS_READ, fd, (uint32_t)(read_buf + 7), SYSCALL_FILE_BUFFER_SIZE - 7) : SYSCALL_EBADF;
    if (result != SYSCALL_FILE_BUFFER_SIZE - 7) {
        data_ok = 0;
    }
    result = data_ok ? syscall3(SYS_READ, fd, (uint32_t)read_buf, 1) : SYSCALL_EBADF;
    if (result != 0) {
        data_ok = 0;
    }
    for (uint32_t i = 0; i < SYSCALL_FILE_BUFFER_SIZE; i++) {
        if (read_buf[i] != write_buf[i]) {
            data_ok = 0;
        }
    }
    if (fd < VFS_MAX_OPEN_FILES && syscall3(SYS_CLOSE, fd, 0, 0) != SYSCALL_SUCCESS) {
        data_ok = 0;
    }
    read_ok = data_ok;
    if (read_ok) {
        syscall_marker(SYSCALL_MARK_FILE_READ);
    } else {
        syscall_marker(SYSCALL_MARK_FILE_READ_FAIL);
    }

    result = read_ok ? syscall3(SYS_STAT, (uint32_t)path, (uint32_t)stat, 0) : SYSCALL_EBADF;
    stat_ok = result == SYSCALL_SUCCESS &&
              stat->size == SYSCALL_FILE_BUFFER_SIZE &&
              stat->allocated_sectors >= 1;
    if (stat_ok) {
        syscall_marker(SYSCALL_MARK_FILE_STAT);
    } else {
        syscall_marker(SYSCALL_MARK_FILE_STAT_FAIL);
    }

    fd = syscall3(SYS_OPEN, (uint32_t)path, SYS_O_RDONLY, 0);
    int bad_read_ok = fd < VFS_MAX_OPEN_FILES &&
                      syscall3(SYS_READ, fd, 0x00100000, 1) == SYSCALL_EFAULT &&
                      syscall3(SYS_CLOSE, fd, 0, 0) == SYSCALL_SUCCESS;
    negative_ok = bad_read_ok &&
                  syscall3(SYS_OPEN, (uint32_t)missing_path, SYS_O_RDONLY, 0) == SYSCALL_ENOENT &&
                  syscall3(SYS_OPEN, (uint32_t)nested_path, SYS_O_CREAT | SYS_O_RDWR, 0) == SYSCALL_EINVAL &&
                  syscall3(SYS_CLOSE, 99, 0, 0) == SYSCALL_EBADF;
    if (negative_ok) {
        syscall_marker(SYSCALL_MARK_FILE_NEGATIVE);
    } else {
        syscall_marker(SYSCALL_MARK_FILE_NEG_FAIL);
    }

    if (open_ok && write_ok && read_ok && stat_ok && negative_ok) {
        syscall_marker(SYSCALL_MARK_FILE_DONE);
    }

    while (1) {
    }
}
#endif

#if defined(ENABLE_SYSCALL_FILE_SELFTEST) || defined(ENABLE_PROCESS_SYSCALL_SELFTEST) || \
    defined(ENABLE_REDTEAM_SELFTEST)
static void map_user_code_span(uint32_t start, uint32_t end) {
    uint32_t first = start & 0xFFFFF000;
    uint32_t last = end & 0xFFFFF000;

    if (first > last) {
        uint32_t tmp = first;
        first = last;
        last = tmp;
    }

    for (uint32_t page = first; page <= last + 0x1000; page += 0x1000) {
        paging_map_page(page, page, PAGE_PRESENT | PAGE_USER);
    }
}
#endif

#if defined(ENABLE_PROCESS_SYSCALL_SELFTEST) || defined(ENABLE_REDTEAM_SELFTEST)
#define PROCESS_EXEC_CODE_OFFSET (ELF_TEST_SEGMENT_OFFSET + 0x20u)

static void process_emit_marker(uint8_t *code, uint32_t *offset, uint8_t marker) {
    code[(*offset)++] = 0x6A;  // push imm8
    code[(*offset)++] = SYS_TEST_MARKER;
    code[(*offset)++] = 0x58;  // pop eax
    code[(*offset)++] = 0x6A;  // push imm8
    code[(*offset)++] = marker;
    code[(*offset)++] = 0x5B;  // pop ebx
    code[(*offset)++] = 0xB9;  // mov ecx, imm32
    elf_put32(code, *offset, SYSCALL_TEST_MARKER_TOKEN);
    *offset += 4;
    code[(*offset)++] = 0x31;  // xor edx, edx
    code[(*offset)++] = 0xD2;
    code[(*offset)++] = 0xCD;  // int 0x80
    code[(*offset)++] = 0x80;
}
#endif

#ifdef ENABLE_PROCESS_SYSCALL_SELFTEST
#define PROCESS_SYSCALL_STACK_PHYSICAL 0x00950000

static void process_set_exec_path(char *path) {
    path[0] = '/';
    path[1] = 'p';
    path[2] = 'e';
    path[3] = 'x';
    path[4] = 'e';
    path[5] = 'c';
    path[6] = '.';
    path[7] = 'e';
    path[8] = 'l';
    path[9] = 'f';
    path[10] = '\0';
}

static void process_make_exec_image(void) {
    uint8_t *code = elf_test_image + PROCESS_EXEC_CODE_OFFSET;
    uint32_t offset = 0;

    elf_make_image(elf_test_image, ELF_TEST_VADDR);
    process_emit_marker(code, &offset, SYSCALL_MARK_PROCESS_EXEC_ENTERED);
    process_emit_marker(code, &offset, SYSCALL_MARK_PROCESS_DONE);
    code[offset++] = 0xEB;  // jmp $
    code[offset++] = 0xFE;

    elf_put32(elf_test_image, 68, 0x20u + offset);
    elf_put32(elf_test_image, 72, ELF_TEST_SEGMENT_MEM_SIZE);
}

static __attribute__((noreturn)) void process_syscall_user_entry(void) {
    char exec_path[16];
    uint32_t status = 0;
    uint32_t result;
    volatile uint32_t *heap0 = (volatile uint32_t *)PROCESS_HEAP_START;
    volatile uint32_t *heap1 = (volatile uint32_t *)(PROCESS_HEAP_START + 0x1000);
    uint32_t brk_target = PROCESS_HEAP_START + 0x1000 + 64;

    result = syscall3(SYS_GETPID, 0, 0, 0);
    if (result != 0) {
        syscall_marker(SYSCALL_MARK_PROCESS_GETPID);
    } else {
        syscall_marker(SYSCALL_MARK_PROCESS_FAIL);
    }

    result = syscall3(SYS_BRK, 0, 0, 0);
    if (result == PROCESS_HEAP_START) {
        syscall_marker(SYSCALL_MARK_PROCESS_BRK_QUERY);
    } else {
        syscall_marker(SYSCALL_MARK_PROCESS_FAIL);
    }

    result = syscall3(SYS_BRK, brk_target, 0, 0);
    if (result == brk_target) {
        syscall_marker(SYSCALL_MARK_PROCESS_BRK_GROW);
    } else {
        syscall_marker(SYSCALL_MARK_PROCESS_FAIL);
    }

    *heap0 = 0xC0DEF00D;
    *heap1 = 0x1234ABCD;
    if (*heap0 == 0xC0DEF00D && *heap1 == 0x1234ABCD) {
        syscall_marker(SYSCALL_MARK_PROCESS_BRK_RW);
    } else {
        syscall_marker(SYSCALL_MARK_PROCESS_FAIL);
    }

    result = syscall3(SYS_BRK, PROCESS_HEAP_START, 0, 0);
    if (result == PROCESS_HEAP_START) {
        syscall_marker(SYSCALL_MARK_PROCESS_BRK_SHRINK);
    } else {
        syscall_marker(SYSCALL_MARK_PROCESS_FAIL);
    }

    result = syscall3(SYS_WAIT, (uint32_t)&status, 0, 0);
    if (result == SYSCALL_ECHILD) {
        syscall_marker(SYSCALL_MARK_PROCESS_WAIT_NEG);
    } else {
        syscall_marker(SYSCALL_MARK_PROCESS_FAIL);
    }

    process_set_exec_path(exec_path);
    result = syscall3(SYS_EXEC, (uint32_t)exec_path, 0, 0);
    (void)result;

    syscall_marker(SYSCALL_MARK_PROCESS_FAIL);
    while (1) {
    }
}
#endif

#ifdef ENABLE_REDTEAM_SELFTEST
#define REDTEAM_STACK_PHYSICAL 0x00960000
#define REDTEAM_DOS_WRITE_SIZE (SECTOR_SIZE * 32u)
#define REDTEAM_HEAP_SENTINEL 0xBADF00Du
#define REDTEAM_EXEC_FD_PROBE_VADDR (ELF_TEST_VADDR + 0xB0u)
#define REDTEAM_EXEC_FAILURE_PATH_VADDR 0x50000000u
#define REDTEAM_EXEC_FAILURE_ATTEMPTS 8u
#define REDTEAM_PROCESS_DESTROY_VADDR 0x51000000u

static uint8_t redteam_dos_buf[REDTEAM_DOS_WRITE_SIZE];

static __attribute__((noreturn)) void redteam_user_entry(void);

static int redteam_scheduler_yield_mixing_blocked_probe(void) {
    process_init();
    scheduler_init();

    struct process *current = process_create_preemptive((uint32_t)redteam_user_entry);
    struct process *other = process_create_preemptive((uint32_t)redteam_user_entry);
    if (!current || !other) {
        return 0;
    }

    scheduler_set_current(current);
    scheduler_add(other);
    uint32_t before_count = scheduler_get_count();
    yield();

    return scheduler_get_current() == current &&
           scheduler_get_count() == before_count &&
           other->state == PROCESS_READY;
}

static int redteam_simplefs_dos_blocked_probe(void) {
    const char *path = "/red-dos";
    struct vfs_stat stat;

    for (uint32_t i = 0; i < REDTEAM_DOS_WRITE_SIZE; i++) {
        redteam_dos_buf[i] = (uint8_t)(i & 0xFF);
    }

    for (uint32_t round = 0; round < 256; round++) {
        int fd = vfs_open(path, VFS_O_CREAT | VFS_O_RDWR | VFS_O_TRUNC);
        if (fd < 0) {
            return 0;
        }

        int status = vfs_write(fd, redteam_dos_buf, REDTEAM_DOS_WRITE_SIZE);
        vfs_close(fd);

        if (status == VFS_ENOSPC) {
            return 0;
        }
        if (status != (int)REDTEAM_DOS_WRITE_SIZE) {
            return 0;
        }
    }

    return vfs_stat(path, &stat) == VFS_SUCCESS &&
           stat.size == REDTEAM_DOS_WRITE_SIZE &&
           stat.allocated_sectors == (REDTEAM_DOS_WRITE_SIZE / SECTOR_SIZE);
}

static int redteam_vfs_namespace_blocked_probe(void) {
    struct vfs_stat stat;

    if (vfs_open("relative", VFS_O_CREAT | VFS_O_RDWR) != VFS_EINVAL) {
        return 0;
    }
    if (vfs_open("/dir/file", VFS_O_CREAT | VFS_O_RDWR) != VFS_EINVAL) {
        return 0;
    }
    if (vfs_open("/.", VFS_O_CREAT | VFS_O_RDWR) != VFS_EINVAL) {
        return 0;
    }
    if (vfs_open("/..", VFS_O_CREAT | VFS_O_RDWR) != VFS_EINVAL) {
        return 0;
    }
    if (vfs_stat("relative", &stat) != VFS_EINVAL) {
        return 0;
    }
    if (vfs_stat("/dir/file", &stat) != VFS_EINVAL) {
        return 0;
    }

    return 1;
}

static int redteam_elf_overlap_blocked_probe(void) {
    struct elf_load_info info;
    const char *path = "/overlap.elf";
    uint32_t overlap_vaddr = ELF_TEST_VADDR + 0x20u;

    elf_make_image(elf_test_image, ELF_TEST_VADDR);
    elf_put16(elf_test_image, 44, 2);

    elf_put32(elf_test_image, 84, 1);
    elf_put32(elf_test_image, 88, ELF_TEST_SEGMENT_OFFSET + 64u);
    elf_put32(elf_test_image, 92, overlap_vaddr);
    elf_put32(elf_test_image, 96, overlap_vaddr);
    elf_put32(elf_test_image, 100, 4u);
    elf_put32(elf_test_image, 104, 64u);
    elf_put32(elf_test_image, 108, 5u);
    elf_put32(elf_test_image, 112, 1u);

    elf_test_image[ELF_TEST_SEGMENT_OFFSET + 64u] = 'O';
    elf_test_image[ELF_TEST_SEGMENT_OFFSET + 65u] = 'V';
    elf_test_image[ELF_TEST_SEGMENT_OFFSET + 66u] = 'R';
    elf_test_image[ELF_TEST_SEGMENT_OFFSET + 67u] = 'L';

    if (!elf_write_fixture(path, elf_test_image, sizeof(elf_test_image))) {
        return 0;
    }

    if (elf_load_from_vfs(path, &info) != ELF_EINVAL) {
        return 0;
    }

    return !paging_is_mapped(ELF_TEST_VADDR);
}

static int redteam_exec_failure_cleanup_blocked_probe(void) {
    const char *path = "/badexec.elf";
    char *user_path = (char *)REDTEAM_EXEC_FAILURE_PATH_VADDR;
    uint32_t path_phys = frame_alloc();
    uint32_t frame_before;
    uint32_t frame_after;
    uint32_t fake_frame[4] = {0, 0, 0, 0};

    if (!path_phys) {
        return 0;
    }

    paging_map_page(REDTEAM_EXEC_FAILURE_PATH_VADDR,
                    path_phys,
                    PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (!paging_is_user_writable(REDTEAM_EXEC_FAILURE_PATH_VADDR)) {
        frame_free(path_phys);
        return 0;
    }

    elf_make_image(elf_test_image, ELF_TEST_VADDR);
    elf_test_image[0] = 'B';
    if (!elf_write_fixture(path, elf_test_image, sizeof(elf_test_image))) {
        paging_unmap_page(REDTEAM_EXEC_FAILURE_PATH_VADDR);
        frame_free(path_phys);
        return 0;
    }

    memcpy(user_path, path, strlen(path) + 1);

    process_init();
    scheduler_init();
    struct process *proc = process_create((uint32_t)redteam_user_entry, 1);
    if (!proc) {
        paging_unmap_page(REDTEAM_EXEC_FAILURE_PATH_VADDR);
        frame_free(path_phys);
        return 0;
    }
    scheduler_set_current(proc);

    frame_before = frame_get_free_count();
    for (uint32_t i = 0; i < REDTEAM_EXEC_FAILURE_ATTEMPTS; i++) {
        uint32_t result = syscall_handler(SYS_EXEC,
                                          REDTEAM_EXEC_FAILURE_PATH_VADDR,
                                          0,
                                          0,
                                          0x1B,
                                          (uint32_t)fake_frame);
        if (result != SYSCALL_EINVAL) {
            paging_unmap_page(REDTEAM_EXEC_FAILURE_PATH_VADDR);
            frame_free(path_phys);
            return 0;
        }
    }
    frame_after = frame_get_free_count();

    paging_unmap_page(REDTEAM_EXEC_FAILURE_PATH_VADDR);
    frame_free(path_phys);
    return frame_before == frame_after;
}

static int redteam_process_destroy_cleanup_blocked_probe(void) {
    uint32_t frame_before = frame_get_free_count();
    uint32_t old_cr3 = paging_get_current_directory();

    process_init();
    struct process *proc = process_create((uint32_t)redteam_user_entry, 1);
    if (!proc) {
        return 0;
    }

    uint32_t proc_cr3 = paging_create_address_space();
    if (!proc_cr3) {
        process_destroy(proc);
        return 0;
    }

    uint32_t data_phys = frame_alloc();
    if (!data_phys) {
        paging_destroy_address_space(proc_cr3);
        process_destroy(proc);
        return 0;
    }

    paging_map_page_in_directory(proc_cr3,
                                 REDTEAM_PROCESS_DESTROY_VADDR,
                                 data_phys,
                                 PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    paging_switch_directory(proc_cr3);
    int mapped = paging_is_user_writable(REDTEAM_PROCESS_DESTROY_VADDR);
    paging_switch_directory(old_cr3);

    if (!mapped) {
        frame_free(data_phys);
        paging_destroy_address_space(proc_cr3);
        process_destroy(proc);
        return 0;
    }

    process_set_address_space(proc, proc_cr3);
    process_destroy(proc);
    return frame_get_free_count() == frame_before;
}

static void redteam_set_exec_path(char *path) {
    path[0] = '/';
    path[1] = 'r';
    path[2] = 'e';
    path[3] = 'd';
    path[4] = 'e';
    path[5] = 'x';
    path[6] = 'e';
    path[7] = 'c';
    path[8] = '.';
    path[9] = 'e';
    path[10] = 'l';
    path[11] = 'f';
    path[12] = '\0';
}

static void redteam_make_exec_image(void) {
    uint8_t *code = elf_test_image + PROCESS_EXEC_CODE_OFFSET;
    uint32_t offset = 0;
    uint32_t heap_jne_patch;
    uint32_t fd_jne_patch;

    elf_make_image(elf_test_image, ELF_TEST_VADDR);
    elf_test_image[ELF_TEST_SEGMENT_OFFSET + 0xB0u] = 'L';

    code[offset++] = 0x6A;  // push imm8
    code[offset++] = SYS_WRITE;
    code[offset++] = 0x58;  // pop eax
    code[offset++] = 0x6A;  // push imm8
    code[offset++] = 99u;
    code[offset++] = 0x5B;  // pop ebx
    code[offset++] = 0xB9;  // mov ecx, imm32
    elf_put32(code, offset, PROCESS_HEAP_START);
    offset += 4;
    code[offset++] = 0x6A;  // push imm8
    code[offset++] = 4u;
    code[offset++] = 0x5A;  // pop edx
    code[offset++] = 0xCD;  // int 0x80
    code[offset++] = 0x80;
    code[offset++] = 0x3D;  // cmp eax, imm32
    elf_put32(code, offset, SYSCALL_EFAULT);
    offset += 4;
    code[offset++] = 0x75;  // jne fail
    heap_jne_patch = offset++;

    process_emit_marker(code, &offset, SYSCALL_MARK_RED_EXEC_BLOCKED);

    code[offset++] = 0x6A;  // push imm8
    code[offset++] = SYS_WRITE;
    code[offset++] = 0x58;  // pop eax
    code[offset++] = 0x31;  // xor ebx, ebx
    code[offset++] = 0xDB;
    code[offset++] = 0xB9;  // mov ecx, imm32
    elf_put32(code, offset, REDTEAM_EXEC_FD_PROBE_VADDR);
    offset += 4;
    code[offset++] = 0x6A;  // push imm8
    code[offset++] = 1u;
    code[offset++] = 0x5A;  // pop edx
    code[offset++] = 0xCD;  // int 0x80
    code[offset++] = 0x80;
    code[offset++] = 0x3D;  // cmp eax, imm32
    elf_put32(code, offset, SYSCALL_EBADF);
    offset += 4;
    code[offset++] = 0x75;  // jne fail
    fd_jne_patch = offset++;

    process_emit_marker(code, &offset, SYSCALL_MARK_RED_EXEC_FD_BLOCKED);
    process_emit_marker(code, &offset, SYSCALL_MARK_RED_DEFENSES_DONE);
    code[offset++] = 0xEB;  // jmp $
    code[offset++] = 0xFE;

    uint32_t fail_offset = offset;
    process_emit_marker(code, &offset, SYSCALL_MARK_RED_FAIL);
    code[offset++] = 0xEB;  // jmp $
    code[offset++] = 0xFE;

    code[heap_jne_patch] = (uint8_t)(fail_offset - (heap_jne_patch + 1));
    code[fd_jne_patch] = (uint8_t)(fail_offset - (fd_jne_patch + 1));
    elf_put32(elf_test_image, 68, 0x20u + offset);
    elf_put32(elf_test_image, 72, 192u);
}

static __attribute__((noreturn)) void redteam_user_entry(void) {
    char exec_path[16];
    char fd_path[16];
    char fd_byte = 'F';
    uint32_t result;
    volatile uint32_t *heap = (volatile uint32_t *)PROCESS_HEAP_START;

    result = syscall3(SYS_TEST_MARKER, SYSCALL_MARK_FILE_DONE, 0, 0);
    if (result == SYSCALL_EPERM) {
        syscall_marker(SYSCALL_MARK_RED_MARKER_BLOCKED);
    } else {
        syscall_marker(SYSCALL_MARK_RED_FAIL);
    }

    result = syscall3(SYS_TEST_ABI, 0x11111111, 0x22222222, 0x33333333);
    if (result == SYSCALL_EPERM) {
        syscall_marker(SYSCALL_MARK_RED_SYSCALL_PRIV_BLOCKED);
    } else {
        syscall_marker(SYSCALL_MARK_RED_FAIL);
    }

    result = syscall3(SYS_BRK, PROCESS_HEAP_START + 4, 0, 0);
    if (result != PROCESS_HEAP_START + 4) {
        syscall_marker(SYSCALL_MARK_RED_FAIL);
    }

    *heap = REDTEAM_HEAP_SENTINEL;
    fd_path[0] = '/';
    fd_path[1] = 'f';
    fd_path[2] = 'd';
    fd_path[3] = 'l';
    fd_path[4] = 'e';
    fd_path[5] = 'a';
    fd_path[6] = 'k';
    fd_path[7] = '\0';
    result = syscall3(SYS_OPEN, (uint32_t)fd_path, SYS_O_CREAT | SYS_O_RDWR | SYS_O_TRUNC, 0);
    if (result != 0) {
        syscall_marker(SYSCALL_MARK_RED_FAIL);
    }
    result = syscall3(SYS_WRITE, 0, (uint32_t)&fd_byte, 1);
    if (result != 1) {
        syscall_marker(SYSCALL_MARK_RED_FAIL);
    }

    redteam_set_exec_path(exec_path);
    result = syscall3(SYS_EXEC, (uint32_t)exec_path, 0, 0);
    (void)result;

    syscall_marker(SYSCALL_MARK_RED_FAIL);
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
    frame_reserve_range(0x00900000, 0x00901000);
    frame_reserve_range(0x00910000, 0x00911000);
    frame_reserve_range(0x00930000, 0x00931000);
    frame_reserve_range(0x00940000, 0x00942000);
    frame_reserve_range(0x00950000, 0x00951000);
    frame_reserve_range(0x00960000, 0x00961000);
    paging_init();
    allocator_init();
    if (ramdisk_init() != BLKDEV_SUCCESS) {
        serial_puts("RAMDISK_INIT_FATAL\n");
        while (1) {
            asm volatile("cli; hlt");
        }
    }
    if (vfs_init(ramdisk_get_device()) != VFS_SUCCESS ||
        vfs_format() != VFS_SUCCESS ||
        vfs_mount() != VFS_SUCCESS) {
        serial_puts("VFS_INIT_FATAL\n");
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

#ifdef ENABLE_VFS_SELFTEST
    {
        const char *message = "hello from vfs";
        uint32_t message_len = (uint32_t)strlen(message);
        uint32_t big_len = (uint32_t)sizeof(vfs_write_buf);
        struct vfs_stat stat;
        int fd;

        serial_puts("VFS_TEST\n");

        int format_ok = vfs_init(ramdisk_get_device()) == VFS_SUCCESS &&
                        vfs_format() == VFS_SUCCESS;
        if (format_ok) {
            serial_puts("VFS_FORMAT_OK\n");
        }

        int mount_ok = format_ok && vfs_mount() == VFS_SUCCESS;
        if (mount_ok) {
            serial_puts("VFS_MOUNT_OK\n");
        }

        fd = mount_ok ? vfs_open("/hello.txt", VFS_O_CREAT | VFS_O_RDWR | VFS_O_TRUNC) : VFS_EIO;
        int create_ok = fd >= 0;
        if (create_ok) {
            serial_puts("VFS_CREATE_OK\n");
        }

        int write_ok = create_ok &&
                       vfs_write(fd, message, message_len) == (int)message_len &&
                       vfs_close(fd) == VFS_SUCCESS;
        if (write_ok) {
            serial_puts("VFS_WRITE_OK\n");
        } else if (create_ok) {
            vfs_close(fd);
        }

        memset(vfs_read_buf, 0, sizeof(vfs_read_buf));
        fd = write_ok ? vfs_open("/hello.txt", VFS_O_RDONLY) : VFS_EIO;
        int read_count = fd >= 0 ? vfs_read(fd, vfs_read_buf, message_len) : VFS_EIO;
        int eof_count = fd >= 0 ? vfs_read(fd, vfs_read_buf + message_len, 1) : VFS_EIO;
        int read_close_ok = fd >= 0 && vfs_close(fd) == VFS_SUCCESS;
        int read_ok = read_count == (int)message_len &&
                      eof_count == 0 &&
                      read_close_ok &&
                      memcmp(vfs_read_buf, message, message_len) == 0;
        if (read_ok) {
            serial_puts("VFS_READ_OK\n");
        }

        for (uint32_t i = 0; i < big_len; i++) {
            vfs_write_buf[i] = (uint8_t)((i * 7 + 3) & 0xFF);
            vfs_read_buf[i] = 0;
        }

        fd = read_ok ? vfs_open("/big.bin", VFS_O_CREAT | VFS_O_RDWR | VFS_O_TRUNC) : VFS_EIO;
        int big_write_ok = fd >= 0 &&
                           vfs_write(fd, vfs_write_buf, big_len) == (int)big_len &&
                           vfs_close(fd) == VFS_SUCCESS;
        if (!big_write_ok && fd >= 0) {
            vfs_close(fd);
        }

        memset(vfs_read_buf, 0, sizeof(vfs_read_buf));
        fd = big_write_ok ? vfs_open("/big.bin", VFS_O_RDONLY) : VFS_EIO;
        int first_read = fd >= 0 ? vfs_read(fd, vfs_read_buf, 17) : VFS_EIO;
        int second_read = fd >= 0 ? vfs_read(fd, vfs_read_buf + 17, big_len - 17) : VFS_EIO;
        int big_eof = fd >= 0 ? vfs_read(fd, vfs_read_buf, 1) : VFS_EIO;
        int offset_close_ok = fd >= 0 && vfs_close(fd) == VFS_SUCCESS;
        int offset_ok = first_read == 17 &&
                        second_read == (int)(big_len - 17) &&
                        big_eof == 0 &&
                        offset_close_ok &&
                        memcmp(vfs_write_buf, vfs_read_buf, big_len) == 0;
        if (offset_ok) {
            serial_puts("VFS_OFFSET_OK\n");
        }

        int stat_ok = vfs_stat("/big.bin", &stat) == VFS_SUCCESS &&
                      stat.size == big_len &&
                      stat.allocated_sectors >= 3;
        if (stat_ok) {
            serial_puts("VFS_STAT_OK\n");
        }

        int ro_fd = vfs_open("/hello.txt", VFS_O_RDONLY);
        int permission_ok = ro_fd >= 0 &&
                            vfs_write(ro_fd, message, 1) == VFS_EBADF &&
                            vfs_close(ro_fd) == VFS_SUCCESS;
        if (!permission_ok) {
            serial_puts("VFS_NEG_PERMISSION_FAIL\n");
        }

        int fds[VFS_MAX_OPEN_FILES];
        int emfile_ok = 1;
        for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
            fds[i] = vfs_open("/hello.txt", VFS_O_RDONLY);
            if (fds[i] < 0) {
                emfile_ok = 0;
            }
        }
        if (vfs_open("/hello.txt", VFS_O_RDONLY) != VFS_EMFILE) {
            emfile_ok = 0;
        }
        for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
            if (fds[i] >= 0) {
                vfs_close(fds[i]);
            }
        }
        if (!emfile_ok) {
            serial_puts("VFS_NEG_EMFILE_FAIL\n");
        }

        int missing_ok = vfs_open("/missing.txt", VFS_O_RDONLY) == VFS_ENOENT;
        int nested_ok = vfs_open("/nested/path.txt", VFS_O_CREAT | VFS_O_RDWR) == VFS_EINVAL;
        int long_name_ok = vfs_open("/abcdefghijklmnopqrstuvwxyzabcdef", VFS_O_CREAT | VFS_O_RDWR) == VFS_EINVAL;
        int bad_flags_ok = vfs_open("/bad.txt", VFS_O_CREAT) == VFS_EINVAL;
        int bad_fd_ok = vfs_read(-1, vfs_read_buf, 1) == VFS_EBADF;
        if (!missing_ok) {
            serial_puts("VFS_NEG_MISSING_FAIL\n");
        }
        if (!nested_ok) {
            serial_puts("VFS_NEG_NESTED_FAIL\n");
        }
        if (!long_name_ok) {
            serial_puts("VFS_NEG_LONG_NAME_FAIL\n");
        }
        if (!bad_flags_ok) {
            serial_puts("VFS_NEG_BAD_FLAGS_FAIL\n");
        }
        if (!bad_fd_ok) {
            serial_puts("VFS_NEG_BAD_FD_FAIL\n");
        }

        int negative_ok = permission_ok &&
                          emfile_ok &&
                          missing_ok &&
                          nested_ok &&
                          long_name_ok &&
                          bad_flags_ok &&
                          bad_fd_ok;
        if (negative_ok) {
            serial_puts("VFS_NEGATIVE_OK\n");
        }

        if (format_ok && mount_ok && create_ok && write_ok && read_ok &&
            offset_ok && stat_ok && negative_ok) {
            serial_puts("VFS_OK\n");
        } else {
            serial_puts("VFS_FAIL\n");
        }
    }
#endif

#ifdef ENABLE_ELF_LOADER_SELFTEST
    {
        struct elf_load_info info;

        serial_puts("ELF_TEST\n");

        elf_make_image(elf_test_image, ELF_TEST_VADDR);
        elf_make_image(elf_bad_image, ELF_TEST_VADDR);
        elf_bad_image[1] = 'X';
        memset(elf_trunc_image, 0, sizeof(elf_trunc_image));
        elf_trunc_image[0] = 0x7F;
        elf_trunc_image[1] = 'E';
        elf_trunc_image[2] = 'L';
        elf_trunc_image[3] = 'F';
        elf_make_image(elf_kernel_addr_image, 0x00100000u);

        int vfs_ok = vfs_format() == VFS_SUCCESS &&
                     vfs_mount() == VFS_SUCCESS &&
                     elf_write_fixture("/init.elf", elf_test_image, sizeof(elf_test_image)) &&
                     elf_write_fixture("/bad.elf", elf_bad_image, sizeof(elf_bad_image)) &&
                     elf_write_fixture("/trunc.elf", elf_trunc_image, sizeof(elf_trunc_image)) &&
                     elf_write_fixture("/kerneladdr.elf", elf_kernel_addr_image, sizeof(elf_kernel_addr_image));
        if (vfs_ok) {
            serial_puts("ELF_VFS_WRITE_OK\n");
        }

        int load_status = vfs_ok ? elf_load_from_vfs("/init.elf", &info) : ELF_EIO;
        int load_ok = load_status == ELF_SUCCESS;
        if (load_ok) {
            serial_puts("ELF_LOAD_OK\n");
        }

        int metadata_ok = load_ok &&
                          info.entry == ELF_TEST_ENTRY &&
                          info.load_start == ELF_TEST_VADDR &&
                          info.load_end == (ELF_TEST_VADDR + ELF_TEST_SEGMENT_MEM_SIZE) &&
                          info.loaded_bytes == ELF_TEST_SEGMENT_FILE_SIZE &&
                          info.segment_count == 1;
        if (metadata_ok) {
            serial_puts("ELF_METADATA_OK\n");
        }

        int segment_ok = metadata_ok && elf_loaded_segment_ok();
        if (segment_ok) {
            serial_puts("ELF_SEGMENT_OK\n");
            serial_puts("ELF_BSS_OK\n");
        }

        int negative_ok = elf_load_from_vfs("/bad.elf", &info) == ELF_EINVAL &&
                          elf_load_from_vfs("/trunc.elf", &info) == ELF_EINVAL &&
                          elf_load_from_vfs("/kerneladdr.elf", &info) == ELF_EINVAL &&
                          elf_load_from_vfs("/missing.elf", &info) == ELF_ENOENT;
        if (negative_ok) {
            serial_puts("ELF_NEGATIVE_OK\n");
        }

        if (vfs_ok && load_ok && metadata_ok && segment_ok && negative_ok) {
            serial_puts("ELF_PREP_OK\n");
        } else {
            serial_puts("ELF_FAIL\n");
        }
    }
#endif

#ifdef ENABLE_SYSCALL_ABI_SELFTEST
    {
        uint32_t result = SYS_TEST_ABI;
        asm volatile(
            "int $0x80"
            : "+a"(result)
            : "b"(0x11111111), "c"(0x22222222), "d"(0x33333333)
            : "memory", "cc"
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

#ifdef ENABLE_SYSCALL_FILE_SELFTEST
    {
        serial_puts("SYSCALL_FILE_TEST\n");

        uint32_t user_stack_page = USER_STACK_TOP - 4096;

        vfs_format();
        vfs_mount();
        map_user_code_span((uint32_t)syscall_marker, (uint32_t)syscall_file_user_entry);
        paging_map_page(user_stack_page,
                        SYSCALL_FILE_STACK_PHYSICAL,
                        PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        paging_map_page(SYSCALL_FILE_DATA_VIRTUAL,
                        SYSCALL_FILE_DATA_PHYSICAL,
                        PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

        enter_user_mode((uint32_t)syscall_file_user_entry, USER_STACK_TOP);

        serial_puts("SYSCALL_FILE_FAIL\n");
        while (1) {
            asm volatile("cli; hlt");
        }
    }
#endif

#ifdef ENABLE_PROCESS_SYSCALL_SELFTEST
    {
        serial_puts("PROCESS_SYSCALL_TEST\n");

        uint32_t user_stack_page = USER_STACK_TOP - 4096;

        vfs_format();
        vfs_mount();
        process_make_exec_image();
        int fixture_ok = elf_write_fixture("/pexec.elf", elf_test_image, sizeof(elf_test_image));
        if (fixture_ok) {
            serial_puts("PROCESS_EXEC_FIXTURE_OK\n");
        }

        process_init();
        scheduler_init();
        struct process *user_proc = process_create((uint32_t)process_syscall_user_entry, 1);

        if (fixture_ok && user_proc) {
            scheduler_set_current(user_proc);
            map_user_code_span((uint32_t)syscall_marker, (uint32_t)process_syscall_user_entry);
            paging_map_page(user_stack_page,
                            PROCESS_SYSCALL_STACK_PHYSICAL,
                            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

            if (paging_is_user_accessible(user_stack_page)) {
                serial_puts("PROCESS_USER_READY\n");
                enter_user_mode((uint32_t)process_syscall_user_entry, USER_STACK_TOP);
            }
        }

        serial_puts("PROCESS_FAIL\n");
        while (1) {
            asm volatile("cli; hlt");
        }
    }
#endif

#ifdef ENABLE_REDTEAM_SELFTEST
    {
        serial_puts("RED_TEAM_TEST\n");

        vfs_format();
        vfs_mount();
        if (redteam_scheduler_yield_mixing_blocked_probe()) {
            serial_puts("RED_SCHED_YIELD_MIXING_BLOCKED\n");
        } else {
            serial_puts("RED_TEAM_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }
        if (redteam_simplefs_dos_blocked_probe()) {
            serial_puts("RED_SIMPLEFS_DOS_BLOCKED\n");
        } else {
            serial_puts("RED_TEAM_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }
        if (redteam_vfs_namespace_blocked_probe()) {
            serial_puts("RED_VFS_NAMESPACE_BLOCKED\n");
        } else {
            serial_puts("RED_TEAM_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }
        if (redteam_elf_overlap_blocked_probe()) {
            serial_puts("RED_ELF_OVERLAP_BLOCKED\n");
        } else {
            serial_puts("RED_TEAM_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }
        if (redteam_exec_failure_cleanup_blocked_probe()) {
            serial_puts("RED_EXEC_FAILURE_CLEANUP_BLOCKED\n");
        } else {
            serial_puts("RED_TEAM_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }
        if (redteam_process_destroy_cleanup_blocked_probe()) {
            serial_puts("RED_PROCESS_DESTROY_CLEANUP_BLOCKED\n");
        } else {
            serial_puts("RED_TEAM_FAIL\n");
            while (1) {
                asm volatile("cli; hlt");
            }
        }

        vfs_format();
        vfs_mount();
        redteam_make_exec_image();
        int fixture_ok = elf_write_fixture("/redexec.elf", elf_test_image, sizeof(elf_test_image));

        process_init();
        scheduler_init();
        struct process *user_proc = process_create((uint32_t)redteam_user_entry, 1);
        uint32_t user_stack_page = USER_STACK_TOP - 4096;

        if (fixture_ok && user_proc) {
            scheduler_set_current(user_proc);
            map_user_code_span((uint32_t)syscall_marker, (uint32_t)redteam_user_entry);
            paging_map_page(user_stack_page,
                            REDTEAM_STACK_PHYSICAL,
                            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

            if (paging_is_user_accessible(user_stack_page)) {
                enter_user_mode((uint32_t)redteam_user_entry, USER_STACK_TOP);
            }
        }

        serial_puts("RED_TEAM_FAIL\n");
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
