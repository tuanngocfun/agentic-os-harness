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

#endif
