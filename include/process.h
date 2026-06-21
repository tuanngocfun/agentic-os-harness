#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "interrupt_frame.h"

#define MAX_PROCESSES 16
#define KERNEL_STACK_SIZE 4096
#define USER_STACK_SIZE 4096
#define USER_STACK_TOP 0xB0000000
#define PROCESS_DEFAULT_PRIORITY 1
#define PROCESS_MAX_PRIORITY 16
#define PROCESS_HEAP_START 0x90000000u
#define PROCESS_HEAP_LIMIT 0xA0000000u
#define PROCESS_MAX_FDS 16
#define PROCESS_FD_CLOEXEC 0x0001u

struct process_fd {
    int32_t handle;
    uint32_t flags;
};

enum process_state {
    PROCESS_DEAD = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE
};

struct process {
    uint32_t pid;
    uint32_t esp;
    uint32_t ebp;
    uint32_t eip;
    uint32_t cr3;
    uint32_t interrupt_frame;
    uint32_t priority;
    uint32_t dynamic_priority;
    uint32_t run_count;
    uint64_t test_marker_permissions;
    enum process_state state;
    uint32_t *kernel_stack;
    uint32_t *user_stack;
    struct process *next;

    // Process lifecycle fields
    uint32_t parent_pid;        // Parent process ID (0 = no parent)
    uint32_t exit_code;         // Exit status code
    uint32_t exited;            // 1 if process has exited
    uint32_t waited;            // 1 if parent has waited on this process
    uint32_t waiting_for_child; // 1 while blocked in SYS_WAIT
    uint32_t wait_status_ptr;   // Parent user pointer supplied to SYS_WAIT
    uint32_t wait_result_status;
    uint32_t wait_completion_pending;

    // Memory management
    uint32_t heap_start;        // User heap start address
    uint32_t heap_end;          // Current heap end (brk)

    // Local descriptors reference refcounted VFS open-file descriptions.
    struct process_fd fds[PROCESS_MAX_FDS];
};

void process_init(void);
struct process *process_create(uint32_t entry_point, int is_user);
struct process *process_create_preemptive(uint32_t entry_point);
struct process *process_fork(struct process *parent,
                             const struct interrupt_frame *parent_frame);
void process_destroy(struct process *proc);
struct process *process_get_current(void);
struct process *process_get_by_pid(uint32_t pid);
struct process *process_find_exited_child(uint32_t parent_pid);
int process_has_child(uint32_t parent_pid);
void process_begin_wait(struct process *proc, uint32_t status_ptr);
void process_cancel_wait(struct process *proc);
void process_mark_exit(struct process *proc, uint32_t exit_code);
void process_complete_context_switch(void);
uint32_t process_get_count(void);
void process_set_address_space(struct process *proc, uint32_t cr3);
/* Kernel-owned, one-shot permissions for selftest marker emission. */
int process_allow_test_marker(struct process *proc, uint32_t marker);
int process_consume_test_marker(struct process *proc, uint32_t marker);
void process_clear_test_markers(struct process *proc);
void process_fd_table_init(struct process *proc);
int process_fd_install(struct process *proc, int handle, uint32_t flags);
int process_fd_resolve(const struct process *proc, int fd);
int process_fd_close(struct process *proc, int fd);
void process_fd_close_on_exec(struct process *proc);
void process_fd_close_all(struct process *proc);
int process_fd_inherit(struct process *child, const struct process *parent);

#endif
