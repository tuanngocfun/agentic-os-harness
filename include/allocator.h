#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

void allocator_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
uint32_t allocator_get_free_bytes(void);
uint32_t allocator_get_used_bytes(void);

#endif
