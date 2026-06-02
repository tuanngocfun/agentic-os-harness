#include "process.h"
#include "string.h"
#include <stdint.h>

static struct process process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t process_count = 0;

static uint32_t *allocate_stack(uint32_t size) {
    static uint32_t stack_pool[16 * 1024];
    static uint32_t stack_offset = 0;

    uint32_t *stack = &stack_pool[stack_offset];
    stack_offset += size / sizeof(uint32_t);
    return stack;
}

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    next_pid = 1;
    process_count = 0;
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
    proc->cr3 = 0;

    proc->kernel_stack = allocate_stack(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
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

void process_destroy(struct process *proc) {
    if (!proc) return;
    proc->state = PROCESS_DEAD;
    proc->pid = 0;
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

uint32_t process_get_count(void) {
    return process_count;
}
