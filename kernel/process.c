#include "process.h"
#include "paging.h"
#include "scheduler.h"
#include "string.h"
#include "vfs.h"
#include "pipe.h"
#include "uaccess.h"
#include "syscall.h"
#include <stdint.h>

#define STACK_SLOTS 16
#define STACK_SLOT_SIZE (KERNEL_STACK_SIZE / sizeof(uint32_t))

static struct process process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t process_count = 0;
static uint32_t stack_pool[STACK_SLOTS * STACK_SLOT_SIZE];
static uint16_t stack_free_bitmap = 0xFFFF;  // All 16 slots free initially

int process_allow_test_marker(struct process *proc, uint32_t marker) {
    uint32_t word;
    uint64_t permission;

    if (!proc || marker == 0 ||
        marker > PROCESS_TEST_MARKER_WORDS * 64u) {
        return 0;
    }
    word = (marker - 1) / 64u;
    permission = 1ULL << ((marker - 1) % 64u);
    proc->test_marker_permissions[word] |= permission;
    return 1;
}

int process_consume_test_marker(struct process *proc, uint32_t marker) {
    uint32_t word;
    uint64_t permission;

    if (!proc || marker == 0 ||
        marker > PROCESS_TEST_MARKER_WORDS * 64u) {
        return 0;
    }
    word = (marker - 1) / 64u;
    permission = 1ULL << ((marker - 1) % 64u);
    if ((proc->test_marker_permissions[word] & permission) == 0) {
        return 0;
    }
    proc->test_marker_permissions[word] &= ~permission;
    return 1;
}

void process_clear_test_markers(struct process *proc) {
    if (proc) {
        memset(proc->test_marker_permissions, 0,
               sizeof(proc->test_marker_permissions));
    }
}

void process_fd_table_init(struct process *proc) {
    if (!proc) {
        return;
    }
    for (int i = 0; i < PROCESS_MAX_FDS; i++) {
        proc->fds[i].kind = PROCESS_FD_NONE;
        proc->fds[i].handle = -1;
        proc->fds[i].flags = 0;
    }
}

int process_fd_install_typed(struct process *proc, enum process_fd_kind kind,
                             int handle, uint32_t flags) {
    if (!proc || kind == PROCESS_FD_NONE || handle < 0 ||
        (flags & ~PROCESS_FD_CLOEXEC) != 0) {
        return VFS_EINVAL;
    }
    for (int fd = 0; fd < PROCESS_MAX_FDS; fd++) {
        if (proc->fds[fd].kind == PROCESS_FD_NONE) {
            proc->fds[fd].kind = kind;
            proc->fds[fd].handle = handle;
            proc->fds[fd].flags = flags;
            return fd;
        }
    }
    return VFS_EMFILE;
}

int process_fd_install(struct process *proc, int handle, uint32_t flags) {
    return process_fd_install_typed(proc, PROCESS_FD_VFS, handle, flags);
}

int process_fd_get(const struct process *proc, int fd,
                   enum process_fd_kind *kind, int *handle) {
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS ||
        proc->fds[fd].kind == PROCESS_FD_NONE || proc->fds[fd].handle < 0) {
        return VFS_EBADF;
    }
    if (kind) {
        *kind = proc->fds[fd].kind;
    }
    if (handle) {
        *handle = proc->fds[fd].handle;
    }
    return VFS_SUCCESS;
}

int process_fd_resolve(const struct process *proc, int fd) {
    int handle;
    return process_fd_get(proc, fd, NULL, &handle) == VFS_SUCCESS ?
        handle : VFS_EBADF;
}

int process_fd_close(struct process *proc, int fd) {
    enum process_fd_kind kind;
    int handle;

    if (process_fd_get(proc, fd, &kind, &handle) != VFS_SUCCESS) {
        return VFS_EBADF;
    }
    proc->fds[fd].kind = PROCESS_FD_NONE;
    proc->fds[fd].handle = -1;
    proc->fds[fd].flags = 0;
    if (kind == PROCESS_FD_PIPE_READ || kind == PROCESS_FD_PIPE_WRITE) {
        return pipe_close(handle) == PIPE_SUCCESS ? VFS_SUCCESS : VFS_EIO;
    }
    return vfs_close(handle);
}

void process_fd_close_on_exec(struct process *proc) {
    if (!proc) {
        return;
    }
    for (int fd = 0; fd < PROCESS_MAX_FDS; fd++) {
        if (proc->fds[fd].kind != PROCESS_FD_NONE &&
            (proc->fds[fd].flags & PROCESS_FD_CLOEXEC) != 0) {
            (void)process_fd_close(proc, fd);
        }
    }
}

void process_fd_close_all(struct process *proc) {
    if (!proc) {
        return;
    }
    for (int fd = 0; fd < PROCESS_MAX_FDS; fd++) {
        if (proc->fds[fd].kind != PROCESS_FD_NONE) {
            (void)process_fd_close(proc, fd);
        }
    }
}

int process_fd_inherit(struct process *child, const struct process *parent) {
    if (!child || !parent) {
        return VFS_EINVAL;
    }
    for (int fd = 0; fd < PROCESS_MAX_FDS; fd++) {
        enum process_fd_kind kind = parent->fds[fd].kind;
        int handle = parent->fds[fd].handle;

        if (kind == PROCESS_FD_NONE) {
            continue;
        }
        if ((kind == PROCESS_FD_PIPE_READ || kind == PROCESS_FD_PIPE_WRITE) ?
                pipe_retain(handle) != PIPE_SUCCESS :
                vfs_retain(handle) != VFS_SUCCESS) {
            process_fd_close_all(child);
            return VFS_EBADF;
        }
        child->fds[fd] = parent->fds[fd];
    }
    return VFS_SUCCESS;
}

static uint32_t *allocate_stack(uint32_t size) {
    if (size != KERNEL_STACK_SIZE) {
        return NULL;
    }

    // Find first free slot
    for (int i = 0; i < STACK_SLOTS; i++) {
        if (stack_free_bitmap & (1 << i)) {
            stack_free_bitmap &= ~(1 << i);  // Mark as used
            return &stack_pool[i * STACK_SLOT_SIZE];
        }
    }
    return NULL;  // No free slots
}

static void free_stack(uint32_t *stack) {
    if (!stack) return;

    // Calculate slot index from stack pointer
    ptrdiff_t offset = stack - stack_pool;
    if (offset < 0 || (size_t)offset >= (STACK_SLOTS * STACK_SLOT_SIZE)) {
        return;  // Invalid stack pointer
    }

    int slot = offset / STACK_SLOT_SIZE;
    if (slot >= 0 && slot < STACK_SLOTS) {
        stack_free_bitmap |= (1 << slot);  // Mark as free
    }
}

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    next_pid = 1;
    process_count = 0;
    stack_free_bitmap = 0xFFFF;  // All slots free
}

struct process *process_create(uint32_t entry_point, int is_user) {
    if (process_count >= MAX_PROCESSES) {
        return NULL;
    }

    struct process *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_DEAD) {
            proc = &process_table[i];
            break;
        }
    }

    if (!proc) {
        return NULL;
    }

    proc->pid = next_pid++;
    proc->state = PROCESS_READY;
    proc->cr3 = paging_get_current_directory();
    proc->interrupt_frame = 0;
    proc->priority = PROCESS_DEFAULT_PRIORITY;
    proc->dynamic_priority = PROCESS_DEFAULT_PRIORITY;
    proc->run_count = 0;
    process_clear_test_markers(proc);

    // Initialize lifecycle fields
    proc->parent_pid = 0;       // No parent initially
    proc->exit_code = 0;
    proc->exited = 0;
    proc->waited = 0;
    proc->waiting_for_child = 0;
    proc->wait_target_pid = 0;
    proc->wait_status_ptr = 0;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;
    proc->wait_is_waitpid = 0;

    // Initialize blocking reason
    proc->block_reason = BLOCK_NONE;
    proc->blocked_fd = -1;
    proc->blocked_buffer = 0;
    proc->blocked_count = 0;

    // Initialize signal state
    process_signal_init(proc);

    // Initialize heap in a high user range below USER_STACK_TOP.
    proc->heap_start = PROCESS_HEAP_START;
    proc->heap_end = PROCESS_HEAP_START;
    process_fd_table_init(proc);

    proc->kernel_stack = allocate_stack(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        proc->state = PROCESS_DEAD;
        proc->pid = 0;
        return NULL;
    }

    uint32_t *stack_top = proc->kernel_stack + (KERNEL_STACK_SIZE / sizeof(uint32_t));

    if (is_user) {
        *(--stack_top) = 0x23;
        *(--stack_top) = USER_STACK_TOP;
        *(--stack_top) = 0x202;
        *(--stack_top) = 0x1B;
        *(--stack_top) = entry_point;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
    } else {
        *(--stack_top) = entry_point;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
        *(--stack_top) = 0;
    }

    proc->esp = (uint32_t)stack_top;
    proc->ebp = 0;
    proc->eip = entry_point;

    process_count++;
    return proc;
}

struct process *process_create_preemptive(uint32_t entry_point) {
    if (process_count >= MAX_PROCESSES) {
        return NULL;
    }

    struct process *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_DEAD) {
            proc = &process_table[i];
            break;
        }
    }

    if (!proc) {
        return NULL;
    }

    proc->pid = next_pid++;
    proc->state = PROCESS_READY;
    proc->cr3 = paging_get_current_directory();
    proc->interrupt_frame = 1;
    proc->priority = PROCESS_DEFAULT_PRIORITY;
    proc->dynamic_priority = PROCESS_DEFAULT_PRIORITY;
    proc->run_count = 0;
    process_clear_test_markers(proc);

    proc->parent_pid = 0;
    proc->exit_code = 0;
    proc->exited = 0;
    proc->waited = 0;
    proc->waiting_for_child = 0;
    proc->wait_status_ptr = 0;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;
    proc->wait_is_waitpid = 0;
    proc->wait_target_pid = 0;
    proc->block_reason = BLOCK_NONE;
    proc->blocked_fd = -1;
    proc->blocked_buffer = 0;
    proc->blocked_count = 0;
    process_signal_init(proc);
    proc->heap_start = PROCESS_HEAP_START;
    proc->heap_end = PROCESS_HEAP_START;
    process_fd_table_init(proc);

    proc->kernel_stack = allocate_stack(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        proc->state = PROCESS_DEAD;
        proc->pid = 0;
        return NULL;
    }

    uint32_t *stack_top = proc->kernel_stack + (KERNEL_STACK_SIZE / sizeof(uint32_t));

    *(--stack_top) = 0x202;
    *(--stack_top) = 0x08;
    *(--stack_top) = entry_point;
    *(--stack_top) = 0x10;
    *(--stack_top) = 0x10;
    *(--stack_top) = 0x10;
    *(--stack_top) = 0x10;
    *(--stack_top) = 0;
    *(--stack_top) = 0;
    *(--stack_top) = 0;
    *(--stack_top) = 0;
    *(--stack_top) = 0;
    *(--stack_top) = 0;
    *(--stack_top) = 0;
    *(--stack_top) = 0;

    proc->esp = (uint32_t)stack_top;
    proc->ebp = 0;
    proc->eip = entry_point;

    process_count++;
    return proc;
}

struct process *process_fork(struct process *parent,
                             const struct interrupt_frame *parent_frame) {
    struct process *child = NULL;
    struct interrupt_frame *child_frame;
    uint32_t child_cr3;

    if (!parent || !parent_frame ||
        (parent_frame->cs & 0x03) != 0x03 ||
        parent->cr3 == 0 ||
        process_count >= MAX_PROCESSES) {
        return NULL;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_DEAD) {
            child = &process_table[i];
            break;
        }
    }
    if (!child) {
        return NULL;
    }

    memset(child, 0, sizeof(*child));
    process_fd_table_init(child);
    child->kernel_stack = allocate_stack(KERNEL_STACK_SIZE);
    if (!child->kernel_stack) {
        return NULL;
    }

    child_cr3 = paging_clone_directory(parent->cr3);
    if (!child_cr3) {
        free_stack(child->kernel_stack);
        memset(child, 0, sizeof(*child));
        return NULL;
    }

    child_frame = (struct interrupt_frame *)
        (child->kernel_stack + (KERNEL_STACK_SIZE / sizeof(uint32_t)) -
         (sizeof(struct interrupt_frame) / sizeof(uint32_t)));
    memcpy(child_frame, parent_frame, sizeof(*child_frame));
    child_frame->eax = 0;
    child_frame->eflags |= 0x200;

    child->pid = next_pid++;
    child->esp = (uint32_t)child_frame;
    child->ebp = child_frame->ebp;
    child->eip = child_frame->eip;
    child->cr3 = child_cr3;
    child->interrupt_frame = 1;
    child->priority = parent->priority;
    child->dynamic_priority = parent->priority;
    child->run_count = 0;
    memcpy(child->test_marker_permissions, parent->test_marker_permissions, sizeof(child->test_marker_permissions));
    child->state = PROCESS_READY;
    child->user_stack = (uint32_t *)child_frame->user_esp;
    child->parent_pid = parent->pid;
    child->block_reason = BLOCK_NONE;
    child->blocked_fd = -1;
    process_signal_init(child);
    child->heap_start = parent->heap_start;
    child->heap_end = parent->heap_end;
    child->next = NULL;

    if (process_fd_inherit(child, parent) != VFS_SUCCESS) {
        paging_destroy_address_space(child_cr3);
        free_stack(child->kernel_stack);
        memset(child, 0, sizeof(*child));
        return NULL;
    }

    parent->interrupt_frame = 1;
    process_count++;
    return child;
}

void process_destroy(struct process *proc) {
    if (!proc || proc->state == PROCESS_DEAD) {
        return;
    }

    process_fd_close_all(proc);
    paging_destroy_address_space(proc->cr3);

    // Free the kernel stack
    free_stack(proc->kernel_stack);

    // Clear process state
    proc->state = PROCESS_DEAD;
    proc->pid = 0;
    proc->kernel_stack = NULL;
    proc->esp = 0;
    proc->ebp = 0;
    proc->eip = 0;
    proc->cr3 = 0;
    proc->interrupt_frame = 0;
    proc->priority = 0;
    proc->dynamic_priority = 0;
    proc->run_count = 0;
    process_clear_test_markers(proc);
    proc->parent_pid = 0;
    proc->exit_code = 0;
    proc->exited = 0;
    proc->waited = 0;
    proc->waiting_for_child = 0;
    proc->wait_status_ptr = 0;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;
    proc->wait_target_pid = 0;
    proc->block_reason = BLOCK_NONE;
    proc->blocked_fd = -1;
    proc->blocked_buffer = 0;
    proc->blocked_count = 0;
    process_signal_init(proc);
    proc->heap_start = 0;
    proc->heap_end = 0;
    proc->user_stack = NULL;
    proc->next = NULL;
    process_fd_table_init(proc);

    if (process_count > 0) {
        process_count--;
    }
}

struct process *process_get_current(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_RUNNING) {
            return &process_table[i];
        }
    }
    return NULL;
}

struct process *process_get_by_pid(uint32_t pid) {
    if (pid == 0) {
        return NULL;
    }
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && process_table[i].state != PROCESS_DEAD) {
            return &process_table[i];
        }
    }
    return NULL;
}

struct process *process_find_exited_child(uint32_t parent_pid) {
    return process_find_exited_child_by_pid(parent_pid, (uint32_t)-1);
}

struct process *process_find_exited_child_by_pid(uint32_t parent_pid,
                                                   uint32_t target_pid) {
    if (parent_pid == 0) {
        return NULL;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid != 0 &&
            process_table[i].parent_pid == parent_pid &&
            process_table[i].state == PROCESS_ZOMBIE &&
            process_table[i].exited &&
            !process_table[i].waited) {
            if (target_pid != (uint32_t)-1 &&
                process_table[i].pid != target_pid) {
                continue;
            }
            return &process_table[i];
        }
    }

    return NULL;
}

int process_has_child_by_pid(uint32_t parent_pid, uint32_t target_pid) {
    if (parent_pid == 0) {
        return 0;
    }
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROCESS_DEAD &&
            process_table[i].parent_pid == parent_pid &&
            !process_table[i].waited &&
            (target_pid == (uint32_t)-1 ||
             process_table[i].pid == target_pid)) {
            return 1;
        }
    }
    return 0;
}

int process_has_child(uint32_t parent_pid) {
    return process_has_child_by_pid(parent_pid, (uint32_t)-1);
}

int process_has_exited_child(uint32_t parent_pid) {
    return process_find_exited_child(parent_pid) != NULL;
}

void process_begin_wait(struct process *proc, uint32_t status_ptr) {
    process_begin_waitpid(proc, status_ptr, (uint32_t)-1);
}

void process_begin_waitpid(struct process *proc, uint32_t status_ptr,
                           uint32_t target_pid) {
    if (!proc) {
        return;
    }
    proc->waiting_for_child = 1;
    proc->wait_target_pid = target_pid;
    proc->wait_status_ptr = status_ptr;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;
    proc->wait_is_waitpid = 0;
    proc->block_reason = BLOCK_WAIT_CHILD;
}


void process_cancel_wait(struct process *proc) {
    if (!proc) {
        return;
    }
    proc->waiting_for_child = 0;
    proc->wait_target_pid = 0;
    proc->wait_status_ptr = 0;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;
    proc->block_reason = BLOCK_NONE;
}

void process_begin_pipe_wait(struct process *proc, enum block_reason reason,
                             int fd, uint32_t buffer, uint32_t count) {
    if (!proc || (reason != BLOCK_PIPE_READ && reason != BLOCK_PIPE_WRITE)) {
        return;
    }
    proc->block_reason = reason;
    proc->blocked_fd = fd;
    proc->blocked_buffer = buffer;
    proc->blocked_count = count;
}

void process_wake_pipe_waiter(int pipe_index, enum block_reason reason) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *proc = &process_table[i];
        enum process_fd_kind kind;
        int handle;

        if (proc->state != PROCESS_BLOCKED || proc->block_reason != reason ||
            process_fd_get(proc, proc->blocked_fd, &kind, &handle) !=
                VFS_SUCCESS) {
            continue;
        }
        if ((kind == PROCESS_FD_PIPE_READ || kind == PROCESS_FD_PIPE_WRITE) &&
            pipe_handle_index(handle) == pipe_index) {
            scheduler_wake(proc);
            return;
        }
    }
}

void process_mark_exit(struct process *proc, uint32_t exit_code) {
    struct process *parent;
    uint32_t encoded_status;

    if (!proc || proc->state == PROCESS_DEAD ||
        proc->state == PROCESS_ZOMBIE) {
        return;
    }
    scheduler_remove(proc);
    if (exit_code & 0x80000000u) {
        proc->exit_code = exit_code;
    } else {
        proc->exit_code = exit_code & 0xFFu;
    }
    proc->exited = 1;
    process_fd_close_all(proc);
    proc->state = PROCESS_ZOMBIE;
    proc->block_reason = BLOCK_NONE;

    encoded_status = proc->killed_by_signal ?
        W_EXITCODE(0, proc->killed_by_signal) : W_EXITCODE(proc->exit_code, 0);

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROCESS_DEAD &&
            process_table[i].parent_pid == proc->pid) {
            process_table[i].parent_pid = 0;
            if (process_table[i].state == PROCESS_ZOMBIE) {
                process_table[i].waited = 1;
            }
        }
    }

    parent = process_get_by_pid(proc->parent_pid);
    if (!parent || parent->state == PROCESS_ZOMBIE) {
        proc->waited = 1;
        return;
    }
    (void)process_send_signal(parent, SIGCHLD);

    if (parent->state == PROCESS_BLOCKED && parent->waiting_for_child &&
        (parent->wait_target_pid == (uint32_t)-1 ||
         parent->wait_target_pid == proc->pid)) {
        parent->wait_result_status = parent->wait_is_waitpid ?
            encoded_status : proc->exit_code;
        parent->wait_completion_pending = 1;
        parent->waiting_for_child = 0;
        parent->block_reason = BLOCK_NONE;
        if (parent->esp) {
            ((struct interrupt_frame *)parent->esp)->eax = proc->pid;
        }
        proc->waited = 1;
        scheduler_wake(parent);
    }
}

void process_mark_signal_exit(struct process *proc, uint32_t signum) {
    if (!proc || (signum != SIGKILL && signum != SIGTERM)) {
        return;
    }
    proc->pending_signals &= ~(1u << signum);
    proc->killed_by_signal = signum;
    process_mark_exit(proc, 0);
}

static void process_complete_pipe_io(struct process *proc) {
    enum process_fd_kind kind;
    int handle;
    int result = PIPE_EBADF;

    if (!proc || (proc->block_reason != BLOCK_PIPE_READ &&
                  proc->block_reason != BLOCK_PIPE_WRITE)) {
        return;
    }
    if (process_fd_get(proc, proc->blocked_fd, &kind, &handle) ==
        VFS_SUCCESS) {
        if (proc->block_reason == BLOCK_PIPE_READ &&
            kind == PROCESS_FD_PIPE_READ) {
            result = pipe_read(handle, (void *)proc->blocked_buffer,
                               proc->blocked_count);
        } else if (proc->block_reason == BLOCK_PIPE_WRITE &&
                   kind == PROCESS_FD_PIPE_WRITE) {
            result = pipe_write(handle, (const void *)proc->blocked_buffer,
                                proc->blocked_count);
        }
    }
    if (proc->esp) {
        struct interrupt_frame *frame = (struct interrupt_frame *)proc->esp;
        if (result == PIPE_BROKEN) {
            frame->eax = SYSCALL_EPIPE;
        } else if (result == PIPE_WOULD_BLOCK) {
            frame->eax = SYSCALL_EAGAIN;
        } else if (result < 0) {
            frame->eax = SYSCALL_EBADF;
        } else {
            frame->eax = (uint32_t)result;
        }
    }
    proc->block_reason = BLOCK_NONE;
    proc->blocked_fd = -1;
    proc->blocked_buffer = 0;
    proc->blocked_count = 0;
}

void process_complete_context_switch(void) {
    struct process *current = process_get_current();

    if (current && current->wait_completion_pending) {
        if (current->wait_status_ptr &&
            copy_to_user(current->wait_status_ptr,
                         &current->wait_result_status,
                         sizeof(current->wait_result_status)) !=
                UACCESS_SUCCESS && current->esp) {
            ((struct interrupt_frame *)current->esp)->eax = SYSCALL_EFAULT;
        }
        current->wait_status_ptr = 0;
        current->wait_result_status = 0;
        current->wait_completion_pending = 0;
    }
    if (current) {
        process_complete_pipe_io(current);
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *proc = &process_table[i];
        struct process *parent;

        if (proc == current || proc->state != PROCESS_ZOMBIE) {
            continue;
        }
        parent = process_get_by_pid(proc->parent_pid);
        if (proc->waited || proc->parent_pid == 0 || !parent ||
            parent->state == PROCESS_ZOMBIE) {
            process_destroy(proc);
        }
    }
}

uint32_t process_get_count(void) {
    return process_count;
}

void process_set_address_space(struct process *proc, uint32_t cr3) {
    if (!proc) {
        return;
    }
    proc->cr3 = cr3;
}

void process_signal_init(struct process *proc) {
    if (!proc) {
        return;
    }
    proc->pending_signals = 0;
    proc->killed_by_signal = 0;
}

int process_send_signal(struct process *proc, uint32_t signum) {
    if (!proc || (signum != SIGKILL && signum != SIGTERM &&
                  signum != SIGCHLD) ||
        proc->state == PROCESS_DEAD || proc->state == PROCESS_ZOMBIE) {
        return -1;
    }
    proc->pending_signals |= 1u << signum;
    return 0;
}

uint32_t process_deliver_signals(uint32_t frame_esp) {
    while (1) {
        struct process *current = process_get_current();
        if (!current || current->state != PROCESS_RUNNING) {
            return frame_esp;
        }
        struct interrupt_frame *frame = (struct interrupt_frame *)frame_esp;
        if (!frame || (frame->cs & 3) != 3) {
            return frame_esp;
        }

        if (current->pending_signals & (1u << SIGKILL)) {
            current->pending_signals &= ~(1u << SIGKILL);
            current->killed_by_signal = SIGKILL;
            process_mark_exit(current, 0);
            frame_esp = scheduler_exit_current(frame_esp);
            continue;
        }
        if (current->pending_signals & (1u << SIGTERM)) {
            current->pending_signals &= ~(1u << SIGTERM);
            current->killed_by_signal = SIGTERM;
            process_mark_exit(current, 0);
            frame_esp = scheduler_exit_current(frame_esp);
            continue;
        }
        if (current->pending_signals & (1u << SIGCHLD)) {
            current->pending_signals &= ~(1u << SIGCHLD);
            continue;
        }
        break;
    }
    return frame_esp;
}
