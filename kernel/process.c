#include "process.h"
#include "paging.h"
#include "scheduler.h"
#include "string.h"
#include <stdint.h>

#define STACK_SLOTS 16
#define STACK_SLOT_SIZE (KERNEL_STACK_SIZE / sizeof(uint32_t))

static struct process process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t process_count = 0;
static uint32_t stack_pool[STACK_SLOTS * STACK_SLOT_SIZE];
static uint16_t stack_free_bitmap = 0xFFFF;  // All 16 slots free initially

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

    // Initialize lifecycle fields
    proc->parent_pid = 0;       // No parent initially
    proc->exit_code = 0;
    proc->exited = 0;
    proc->waited = 0;
    proc->waiting_for_child = 0;
    proc->wait_status_ptr = 0;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;

    // Initialize heap in a high user range below USER_STACK_TOP.
    proc->heap_start = PROCESS_HEAP_START;
    proc->heap_end = PROCESS_HEAP_START;

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

    proc->parent_pid = 0;
    proc->exit_code = 0;
    proc->exited = 0;
    proc->waited = 0;
    proc->waiting_for_child = 0;
    proc->wait_status_ptr = 0;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;
    proc->heap_start = PROCESS_HEAP_START;
    proc->heap_end = PROCESS_HEAP_START;

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
    child->state = PROCESS_READY;
    child->user_stack = (uint32_t *)child_frame->user_esp;
    child->parent_pid = parent->pid;
    child->heap_start = parent->heap_start;
    child->heap_end = parent->heap_end;
    child->next = NULL;

    parent->interrupt_frame = 1;
    process_count++;
    return child;
}

void process_destroy(struct process *proc) {
    if (!proc || proc->state == PROCESS_DEAD) {
        return;
    }

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
    proc->parent_pid = 0;
    proc->exit_code = 0;
    proc->exited = 0;
    proc->waited = 0;
    proc->waiting_for_child = 0;
    proc->wait_status_ptr = 0;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;
    proc->heap_start = 0;
    proc->heap_end = 0;
    proc->user_stack = NULL;
    proc->next = NULL;

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
    if (parent_pid == 0) {
        return NULL;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid != 0 &&
            process_table[i].parent_pid == parent_pid &&
            process_table[i].state == PROCESS_ZOMBIE &&
            process_table[i].exited &&
            !process_table[i].waited) {
            return &process_table[i];
        }
    }

    return NULL;
}

int process_has_child(uint32_t parent_pid) {
    if (parent_pid == 0) {
        return 0;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROCESS_DEAD &&
            process_table[i].parent_pid == parent_pid &&
            !process_table[i].waited) {
            return 1;
        }
    }
    return 0;
}

void process_begin_wait(struct process *proc, uint32_t status_ptr) {
    if (!proc) {
        return;
    }
    proc->waiting_for_child = 1;
    proc->wait_status_ptr = status_ptr;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;
}

void process_cancel_wait(struct process *proc) {
    if (!proc) {
        return;
    }
    proc->waiting_for_child = 0;
    proc->wait_status_ptr = 0;
    proc->wait_result_status = 0;
    proc->wait_completion_pending = 0;
}

void process_mark_exit(struct process *proc, uint32_t exit_code) {
    struct process *parent;

    if (!proc || proc->state == PROCESS_DEAD) {
        return;
    }

    proc->exit_code = exit_code;
    proc->exited = 1;
    proc->state = PROCESS_ZOMBIE;

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

    if (parent->state == PROCESS_BLOCKED && parent->waiting_for_child) {
        parent->wait_result_status = exit_code;
        parent->wait_completion_pending = 1;
        parent->waiting_for_child = 0;
        if (parent->esp) {
            ((struct interrupt_frame *)parent->esp)->eax = proc->pid;
        }
        proc->waited = 1;
        parent->state = PROCESS_READY;
        scheduler_add(parent);
    }
}

void process_complete_context_switch(void) {
    struct process *current = process_get_current();

    if (current && current->wait_completion_pending) {
        if (current->wait_status_ptr) {
            *((uint32_t *)current->wait_status_ptr) = current->wait_result_status;
        }
        current->wait_status_ptr = 0;
        current->wait_result_status = 0;
        current->wait_completion_pending = 0;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *proc = &process_table[i];
        struct process *parent;

        if (proc == current || proc->state != PROCESS_ZOMBIE) {
            continue;
        }

        parent = process_get_by_pid(proc->parent_pid);
        if (proc->waited || proc->parent_pid == 0 ||
            !parent || parent->state == PROCESS_ZOMBIE) {
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
