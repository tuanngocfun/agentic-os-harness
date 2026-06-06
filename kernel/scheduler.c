#include "scheduler.h"
#include "process.h"
#include "paging.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

extern void context_switch(uint32_t *save_esp, uint32_t *load_esp);

static struct process *ready_queue = NULL;
static struct process *current = NULL;
static uint32_t schedule_count = 0;
static int scheduler_initialized = 0;
static int preemption_enabled = 0;

static uint32_t irq_save(void) {
    uint32_t flags;
    asm volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    asm volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

static void switch_address_space(struct process *proc) {
    if (proc && proc->cr3 && proc->cr3 != paging_get_current_directory()) {
        paging_switch_directory(proc->cr3);
    }
}

static void enqueue(struct process *proc) {
    proc->next = NULL;
    if (proc->dynamic_priority == 0) {
        proc->dynamic_priority = proc->priority ? proc->priority : PROCESS_DEFAULT_PRIORITY;
    }
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

static struct process *find_best_ready(void) {
    struct process *best = ready_queue;
    struct process *proc = ready_queue;

    while (proc) {
        if (proc->dynamic_priority > best->dynamic_priority) {
            best = proc;
        }
        proc = proc->next;
    }

    return best;
}

static struct process *dequeue_best(void) {
    struct process *best = find_best_ready();
    if (!best) return NULL;

    if (ready_queue == best) {
        ready_queue = best->next;
    } else {
        struct process *prev = ready_queue;
        while (prev && prev->next != best) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = best->next;
        }
    }

    best->next = NULL;
    return best;
}

static void age_ready_queue(void) {
    struct process *proc = ready_queue;
    while (proc) {
        if (proc->dynamic_priority < PROCESS_MAX_PRIORITY) {
            proc->dynamic_priority++;
        }
        proc = proc->next;
    }
}

static void scheduler_schedule_locked(void) {
    struct process *next = dequeue_best();
    if (!next) {
        return;
    }

    if (current && current->state == PROCESS_RUNNING) {
        current->state = PROCESS_READY;
        enqueue(current);
    }

    age_ready_queue();
    next->dynamic_priority = next->priority ? next->priority : PROCESS_DEFAULT_PRIORITY;
    next->state = PROCESS_RUNNING;
    next->run_count++;
    current = next;
    switch_address_space(current);
    schedule_count++;
}

void scheduler_init(void) {
    ready_queue = NULL;
    current = NULL;
    schedule_count = 0;
    scheduler_initialized = 1;
    preemption_enabled = 0;
}

void scheduler_add(struct process *proc) {
    if (!proc) return;
    uint32_t flags = irq_save();
    enqueue(proc);
    irq_restore(flags);
}

void scheduler_schedule(void) {
    uint32_t flags = irq_save();
    scheduler_schedule_locked();
    irq_restore(flags);
}

void scheduler_tick(void) {
    if (!scheduler_initialized) return;
    if (!ready_queue) return;
}

uint32_t scheduler_preempt(uint32_t interrupted_esp) {
    uint32_t flags = irq_save();

    if (!scheduler_initialized || !preemption_enabled || !ready_queue) {
        irq_restore(flags);
        return interrupted_esp;
    }

    if (current) {
        if (!current->interrupt_frame) {
            irq_restore(flags);
            return interrupted_esp;
        }
        current->esp = interrupted_esp;
    }

    struct process *next = find_best_ready();
    if (!next->interrupt_frame) {
        irq_restore(flags);
        return interrupted_esp;
    }

    scheduler_schedule_locked();

    if (!current || !current->interrupt_frame || current->esp == 0) {
        irq_restore(flags);
        return interrupted_esp;
    }

    uint32_t next_esp = current->esp;
    irq_restore(flags);
    return next_esp;
}

void scheduler_set_preemption_enabled(int enabled) {
    uint32_t flags = irq_save();
    preemption_enabled = enabled ? 1 : 0;
    irq_restore(flags);
}

void scheduler_set_priority(struct process *proc, uint32_t priority) {
    if (!proc) return;
    if (priority == 0) priority = PROCESS_DEFAULT_PRIORITY;
    if (priority > PROCESS_MAX_PRIORITY) priority = PROCESS_MAX_PRIORITY;

    uint32_t flags = irq_save();
    proc->priority = priority;
    proc->dynamic_priority = priority;
    irq_restore(flags);
}

uint32_t scheduler_get_count(void) {
    return schedule_count;
}

struct process *scheduler_get_current(void) {
    return current;
}

void scheduler_set_current(struct process *proc) {
    uint32_t flags = irq_save();
    current = proc;
    if (proc) {
        proc->state = PROCESS_RUNNING;
        switch_address_space(proc);
    }
    irq_restore(flags);
}

void yield(void) {
    if (!scheduler_initialized) return;

    struct process *prev = current;
    if (prev && prev->interrupt_frame) {
        return;
    }

    scheduler_schedule();

    if (prev && prev != current && prev->state == PROCESS_READY) {
        context_switch(&prev->esp, &current->esp);
    }
}
