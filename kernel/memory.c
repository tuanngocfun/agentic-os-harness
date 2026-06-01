#include "memory.h"
#include <stdint.h>

static uint32_t total_memory_kb = 0;

void memory_init(void) {
    total_memory_kb = 512 * 1024;
}

uint32_t memory_get_total_kb(void) {
    return total_memory_kb;
}
