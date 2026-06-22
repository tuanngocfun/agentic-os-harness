#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

void syscall_init(void);
uint32_t syscall_handler(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3,
                         uint32_t caller_cs, uint32_t frame_ptr);
uint32_t syscall_dispatch(uint32_t frame_esp);

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
#define SYS_WAITPID 20
#define SYS_KILL    21
#define SYS_SIGPENDING 22
#define SYS_PIPE    23
#define SYS_MAX     23

/* waitpid options */
#define WNOHANG     0x0001u

/* Wait status encoding (POSIX-like) */
#define W_EXITCODE(code, sig)  (((code) & 0xFF) << 8 | ((sig) & 0x7F))
#define WIFEXITED(status)      (((status) & 0x7F) == 0)
#define WEXITSTATUS(status)    (((status) >> 8) & 0xFF)
#define WIFSIGNALED(status)    (((status) & 0x7F) != 0)
#define WTERMSIG(status)       ((status) & 0x7F)

/* Signal numbers */
#define SIGKILL   9
#define SIGTERM  15
#define SIGCHLD  17
#define NSIG     32

#define SYS_O_RDONLY 0x0001
#define SYS_O_WRONLY 0x0002
#define SYS_O_RDWR   (SYS_O_RDONLY | SYS_O_WRONLY)
#define SYS_O_CREAT  0x0100
#define SYS_O_TRUNC  0x0200
#define SYS_O_CLOEXEC 0x0400

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
#define SYSCALL_EAGAIN      ((uint32_t)-13) // Resource temporarily unavailable
#define SYSCALL_E2BIG       ((uint32_t)-14) // Argument or environment vector too large
#define SYSCALL_EPIPE       ((uint32_t)-15) // Broken pipe
#define SYSCALL_ESRCH       ((uint32_t)-16) // No such process
#define SYSCALL_EINTR       ((uint32_t)-17) // Interrupted by signal

// User-space address range (0x40000000 - 0xBFFFFFFF)
#define USER_SPACE_START    0x40000000
#define USER_SPACE_END      0xC0000000

#define EXEC_MAX_ARGS          16u
#define EXEC_MAX_ENVS          16u
#define EXEC_MAX_STRING_BYTES  256u
#define EXEC_MAX_VECTOR_BYTES  3072u

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
#define SYSCALL_MARK_LIFECYCLE_FORK_PARENT 36
#define SYSCALL_MARK_LIFECYCLE_FORK_CHILD 37
#define SYSCALL_MARK_LIFECYCLE_WAIT_REAP 38
#define SYSCALL_MARK_LIFECYCLE_ISOLATION 39
#define SYSCALL_MARK_LIFECYCLE_EXEC_REPLACED 40
#define SYSCALL_MARK_LIFECYCLE_DONE 41
#define SYSCALL_MARK_LIFECYCLE_FAIL 42
#define SYSCALL_MARK_RED_MARKER_REPLAY_BLOCKED 43
#define SYSCALL_MARK_EXEC_ARGS_NEGATIVE 44
#define SYSCALL_MARK_EXEC_ARGS_LAYOUT 45
#define SYSCALL_MARK_EXEC_ARGS_CONTENT 46
#define SYSCALL_MARK_EXEC_ARGS_DONE 47
#define SYSCALL_MARK_EXEC_ARGS_FAIL 48
#define SYSCALL_MARK_VM_DEMAND_ZERO 49
#define SYSCALL_MARK_VM_COW_SHARED 50
#define SYSCALL_MARK_VM_COW_SPLIT 51
#define SYSCALL_MARK_VM_COW_ISOLATION 52
#define SYSCALL_MARK_VM_FRAME_ACCOUNTING 53
#define SYSCALL_MARK_VM_GUARD_TERMINATION 54
#define SYSCALL_MARK_VM_DONE 55
#define SYSCALL_MARK_VM_FAIL 56
#define SYSCALL_MARK_WAITPID_WNOHANG 57
#define SYSCALL_MARK_WAITPID_SPECIFIC 58
#define SYSCALL_MARK_WAITPID_STATUS 59
#define SYSCALL_MARK_WAITPID_NEGATIVE 60
#define SYSCALL_MARK_WAITPID_DONE 61
#define SYSCALL_MARK_WAITPID_FAIL 62
#define SYSCALL_MARK_SIGNAL_KILL 63
#define SYSCALL_MARK_SIGNAL_IGN 64
#define SYSCALL_MARK_PIPE_CREATE 65
#define SYSCALL_MARK_PIPE_IO 66
#define SYSCALL_MARK_PIPE_EOF 67
#define SYSCALL_MARK_IPC_DONE 68
#define SYSCALL_MARK_IPC_FAIL 69

#endif
