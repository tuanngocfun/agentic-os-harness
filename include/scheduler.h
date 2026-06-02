#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

void scheduler_init(void);
void scheduler_add(struct process *proc);
void scheduler_schedule(void);
void scheduler_tick(void);
uint32_t scheduler_get_count(void);
struct process *scheduler_get_current(void);
void scheduler_set_current(struct process *proc);
void yield(void);

#endif
