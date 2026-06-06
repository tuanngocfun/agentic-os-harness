#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

#define FRAME_SIZE 4096

void frame_init(void);
uint32_t frame_alloc(void);
uint32_t frame_alloc_below(uint32_t limit);
void frame_free(uint32_t phys_addr);
int frame_is_allocated(uint32_t phys_addr);
uint32_t frame_get_total_count(void);
uint32_t frame_get_free_count(void);
uint32_t frame_get_used_count(void);

#endif
