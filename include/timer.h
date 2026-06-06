#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(uint32_t freq);
uint32_t timer_interrupt(uint32_t interrupted_esp);
uint32_t timer_get_ticks(void);
uint32_t timer_get_seconds(void);

#endif
