#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

void scheduler_init(void);
void scheduler_add(struct process *proc);
void scheduler_schedule(void);
void scheduler_tick(void);

#endif
