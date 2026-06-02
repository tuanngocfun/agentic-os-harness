#include "scheduler.h"
#include "process.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

static struct process *ready_queue = NULL;
static struct process *current = NULL;
static uint32_t schedule_count = 0;

static void enqueue(struct process *proc) {
    proc->next = NULL;
    if (!ready_queue) {
        ready_queue = proc;
    } else {
        struct process *p = ready_queue;
        while (p->next) {
            p = p->next;
        }
        p->next = proc;
    }
}

static struct process *dequeue(void) {
    if (!ready_queue) return NULL;
    struct process *proc = ready_queue;
    ready_queue = ready_queue->next;
    proc->next = NULL;
    return proc;
}

void scheduler_init(void) {
    ready_queue = NULL;
    current = NULL;
    schedule_count = 0;
}

void scheduler_add(struct process *proc) {
    if (!proc) return;
    enqueue(proc);
}

void scheduler_schedule(void) {
    struct process *next = dequeue();
    if (!next) {
        return;
    }

    if (current && current->state == PROCESS_RUNNING) {
        current->state = PROCESS_READY;
        enqueue(current);
    }

    next->state = PROCESS_RUNNING;
    current = next;
    schedule_count++;
}

void scheduler_tick(void) {
    scheduler_schedule();
}

uint32_t scheduler_get_count(void) {
    return schedule_count;
}

struct process *scheduler_get_current(void) {
    return current;
}

void scheduler_set_current(struct process *proc) {
    current = proc;
    if (proc) {
        proc->state = PROCESS_RUNNING;
    }
}

void yield(void) {
    scheduler_schedule();
}
