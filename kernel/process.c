#include "process.h"
#include "paging.h"
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

void process_destroy(struct process *proc) {
    if (!proc || proc->state == PROCESS_DEAD) {
        return;
    }

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

    process_count--;
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

uint32_t process_get_count(void) {
    return process_count;
}

void process_set_address_space(struct process *proc, uint32_t cr3) {
    if (!proc) {
        return;
    }
    proc->cr3 = cr3;
}
