#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

void syscall_init(void);
uint32_t syscall_handler(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t caller_cs);

#define SYS_PUTS    1
#define SYS_GETCHAR 2
#define SYS_CLEAR   3
#define SYS_UPTIME  4
#define SYS_ECHO    5
#define SYS_TEST_ABI 6
#define SYS_USERMODE_TEST 7
#define SYS_TEST_MARKER 8
#define SYS_MAX     8

// Error codes
#define SYSCALL_SUCCESS     0
#define SYSCALL_EINVAL      ((uint32_t)-1)  // Invalid argument
#define SYSCALL_EFAULT      ((uint32_t)-2)  // Bad address
#define SYSCALL_EPERM       ((uint32_t)-3)  // Permission denied
#define SYSCALL_ENOSYS      ((uint32_t)-4)  // Invalid syscall number

// User-space address range (0x40000000 - 0xBFFFFFFF)
#define USER_SPACE_START    0x40000000
#define USER_SPACE_END      0xC0000000

#define SYSCALL_MARK_INVALID_NUM       1
#define SYSCALL_MARK_ZERO_NUM          2
#define SYSCALL_MARK_BAD_POINTER       3
#define SYSCALL_MARK_UNMAPPED_POINTER  4
#define SYSCALL_MARK_RING3_OK          5
#define SYSCALL_MARK_DONE              6

#endif
