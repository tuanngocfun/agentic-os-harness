#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

#define FRAME_SIZE 4096

void frame_init(void);
uint32_t frame_alloc(void);
uint32_t frame_alloc_below(uint32_t limit);
int frame_retain(uint32_t phys_addr);
int frame_release(uint32_t phys_addr);
void frame_free(uint32_t phys_addr);
int frame_reserve_range(uint32_t start_addr, uint32_t end_addr);
int frame_is_allocated(uint32_t phys_addr);
uint32_t frame_get_refcount(uint32_t phys_addr);
uint32_t frame_get_total_count(void);
uint32_t frame_get_free_count(void);
uint32_t frame_get_used_count(void);

#if defined(ENABLE_VM_SELFTEST) || defined(ENABLE_REDTEAM_SELFTEST)
void frame_test_fail_after(int32_t successful_allocations);
#endif

#endif
