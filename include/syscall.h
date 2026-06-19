#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

void syscall_init(void);
uint32_t syscall_handler(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3,
                         uint32_t caller_cs, uint32_t frame_ptr);

#define SYS_PUTS    1
#define SYS_GETCHAR 2
#define SYS_CLEAR   3
#define SYS_UPTIME  4
#define SYS_ECHO    5
#define SYS_TEST_ABI 6
#define SYS_USERMODE_TEST 7
#define SYS_TEST_MARKER 8
#define SYS_OPEN    9
#define SYS_READ    10
#define SYS_WRITE   11
#define SYS_CLOSE   12
#define SYS_STAT    13
#define SYS_GETPID  14
#define SYS_EXIT    15
#define SYS_WAIT    16
#define SYS_FORK    17
#define SYS_EXEC    18
#define SYS_BRK     19
#define SYS_MAX     19

#define SYS_O_RDONLY 0x0001
#define SYS_O_WRONLY 0x0002
#define SYS_O_RDWR   (SYS_O_RDONLY | SYS_O_WRONLY)
#define SYS_O_CREAT  0x0100
#define SYS_O_TRUNC  0x0200

struct syscall_file_stat {
    uint32_t size;
    uint32_t start_sector;
    uint32_t allocated_sectors;
    uint32_t flags;
};

// Error codes
#define SYSCALL_SUCCESS     0
#define SYSCALL_EINVAL      ((uint32_t)-1)  // Invalid argument
#define SYSCALL_EFAULT      ((uint32_t)-2)  // Bad address
#define SYSCALL_EPERM       ((uint32_t)-3)  // Permission denied
#define SYSCALL_ENOSYS      ((uint32_t)-4)  // Invalid syscall number
#define SYSCALL_ENOENT      ((uint32_t)-5)  // No such file
#define SYSCALL_ENOSPC      ((uint32_t)-6)  // No space left
#define SYSCALL_EIO         ((uint32_t)-7)  // I/O error
#define SYSCALL_EMFILE      ((uint32_t)-8)  // Too many open files
#define SYSCALL_EBADF       ((uint32_t)-9)  // Bad file descriptor
#define SYSCALL_EEXIST      ((uint32_t)-10) // File exists
#define SYSCALL_ECHILD      ((uint32_t)-11) // No child process
#define SYSCALL_ENOMEM      ((uint32_t)-12) // Out of memory

// User-space address range (0x40000000 - 0xBFFFFFFF)
#define USER_SPACE_START    0x40000000
#define USER_SPACE_END      0xC0000000

#define SYSCALL_MARK_INVALID_NUM       1
#define SYSCALL_MARK_ZERO_NUM          2
#define SYSCALL_MARK_BAD_POINTER       3
#define SYSCALL_MARK_UNMAPPED_POINTER  4
#define SYSCALL_MARK_RING3_OK          5
#define SYSCALL_MARK_DONE              6
#define SYSCALL_MARK_FILE_OPEN         7
#define SYSCALL_MARK_FILE_WRITE        8
#define SYSCALL_MARK_FILE_READ         9
#define SYSCALL_MARK_FILE_STAT         10
#define SYSCALL_MARK_FILE_NEGATIVE     11
#define SYSCALL_MARK_FILE_DONE         12
#define SYSCALL_MARK_FILE_OPEN_FAIL    13
#define SYSCALL_MARK_FILE_WRITE_FAIL   14
#define SYSCALL_MARK_FILE_READ_FAIL    15
#define SYSCALL_MARK_FILE_STAT_FAIL    16
#define SYSCALL_MARK_FILE_NEG_FAIL     17
#define SYSCALL_MARK_PROCESS_GETPID    18
#define SYSCALL_MARK_PROCESS_BRK_QUERY 19
#define SYSCALL_MARK_PROCESS_BRK_GROW  20
#define SYSCALL_MARK_PROCESS_BRK_RW    21
#define SYSCALL_MARK_PROCESS_BRK_SHRINK 22
#define SYSCALL_MARK_PROCESS_WAIT_NEG  23
#define SYSCALL_MARK_PROCESS_EXEC_ENTERED 24
#define SYSCALL_MARK_PROCESS_DONE      25
#define SYSCALL_MARK_PROCESS_FAIL      26
#define SYSCALL_MARK_RED_MARKER_BLOCKED 27
#define SYSCALL_MARK_RED_EXEC_BLOCKED   28
#define SYSCALL_MARK_RED_DEFENSES_DONE  29
#define SYSCALL_MARK_RED_FAIL          30
#define SYSCALL_MARK_RED_EXEC_FD_BLOCKED 31
#define SYSCALL_MARK_RED_ELF_OVERLAP_BLOCKED 32
#define SYSCALL_MARK_RED_EXEC_FAILURE_CLEANUP_BLOCKED 33
#define SYSCALL_MARK_RED_PROCESS_DESTROY_CLEANUP_BLOCKED 34
#define SYSCALL_MARK_RED_SYSCALL_PRIV_BLOCKED 35

#define SYSCALL_TEST_MARKER_TOKEN 0x51A7C0DEu

#endif
