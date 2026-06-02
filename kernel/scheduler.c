#include "scheduler.h"
#include "process.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

static struct process *ready_queue = NULL;
static struct process *current = NULL;
static uint32_t schedule_count = 0;
static int scheduler_initialized = 0;

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
    scheduler_initialized = 1;
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
    if (!scheduler_initialized) return;
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

uint32_t *scheduler_get_current_esp_ptr(void) {
    if (!scheduler_initialized || !current) {
        return NULL;
    }
    return &current->esp;
}

uint32_t *scheduler_get_next_esp(void) {
    struct process *next = ready_queue;
    if (next) {
        return (uint32_t *)next->esp;
    }
    return NULL;
}

void yield(void) {
    if (!scheduler_initialized) return;

    extern void context_switch(uint32_t *save_esp, uint32_t *load_esp);

    uint32_t *current_esp_ptr = scheduler_get_current_esp_ptr();
    if (!current_esp_ptr) return;

    scheduler_schedule();

    uint32_t *next_esp_ptr = scheduler_get_current_esp_ptr();
    if (!next_esp_ptr || next_esp_ptr == current_esp_ptr) return;

    context_switch(current_esp_ptr, next_esp_ptr);
}
