#include "timer.h"
#include "scheduler.h"
#include <stdint.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_FREQ     1193182

static volatile uint32_t timer_ticks = 0;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void timer_handler(void) {
    timer_ticks++;
    scheduler_tick();
}

void timer_init(uint32_t freq) {
    uint32_t divisor = PIT_FREQ / freq;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    timer_ticks = 0;
}

uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

uint32_t timer_get_seconds(void) {
    return timer_ticks / 100;
}
