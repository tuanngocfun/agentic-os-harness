#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

void scheduler_init(void);
void scheduler_add(struct process *proc);
void scheduler_schedule(void);
void scheduler_tick(void);
uint32_t scheduler_preempt(uint32_t interrupted_esp);
uint32_t scheduler_block_current(uint32_t interrupted_esp);
uint32_t scheduler_exit_current(uint32_t interrupted_esp);
void scheduler_set_preemption_enabled(int enabled);
void scheduler_set_priority(struct process *proc, uint32_t priority);
uint32_t scheduler_get_count(void);
struct process *scheduler_get_current(void);
void scheduler_set_current(struct process *proc);
void yield(void);

#endif
