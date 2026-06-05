#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

void memory_init(void);
uint32_t memory_get_total_kb(void);
int memory_detected_from_hardware(void);

#endif
