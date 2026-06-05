#include "memory.h"
#include <stdint.h>

static uint32_t total_memory_kb = 0;
static int detected_from_hardware = 0;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

void memory_init(void) {
    uint32_t below_16mb_kb = (uint32_t)cmos_read(0x30) |
                             ((uint32_t)cmos_read(0x31) << 8);
    uint32_t above_16mb_blocks = (uint32_t)cmos_read(0x34) |
                                 ((uint32_t)cmos_read(0x35) << 8);

    if (above_16mb_blocks > 0) {
        total_memory_kb = (16 * 1024) + (above_16mb_blocks * 64);
        detected_from_hardware = 1;
    } else if (below_16mb_kb > 0) {
        total_memory_kb = 1024 + below_16mb_kb;
        detected_from_hardware = 1;
    } else {
        total_memory_kb = 0;
        detected_from_hardware = 0;
    }
}

uint32_t memory_get_total_kb(void) {
    return total_memory_kb;
}

int memory_detected_from_hardware(void) {
    return detected_from_hardware;
}
