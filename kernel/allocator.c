#include "allocator.h"
#include <stddef.h>
#include <stdint.h>

#define HEAP_START 0x00200000
#define HEAP_SIZE  0x00100000
#define ALIGNMENT  16
#define MIN_SPLIT  32

struct heap_block {
    uint32_t size;
    uint32_t free;
    struct heap_block *next;
    struct heap_block *prev;
};

static struct heap_block *heap_head = NULL;
static uint32_t heap_used = 0;

static uint32_t align_up(uint32_t value) {
    return (value + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}

void allocator_init(void) {
    heap_head = (struct heap_block *)HEAP_START;
    heap_head->size = HEAP_SIZE - sizeof(struct heap_block);
    heap_head->free = 1;
    heap_head->next = NULL;
    heap_head->prev = NULL;
    heap_used = 0;
}

static void split_block(struct heap_block *block, uint32_t size) {
    uint32_t remaining = block->size - size;
    if (remaining <= sizeof(struct heap_block) + MIN_SPLIT) {
        return;
    }

    struct heap_block *next = (struct heap_block *)((uint8_t *)(block + 1) + size);
    next->size = remaining - sizeof(struct heap_block);
    next->free = 1;
    next->next = block->next;
    next->prev = block;

    if (next->next) {
        next->next->prev = next;
    }

    block->size = size;
    block->next = next;
}

void *kmalloc(size_t size) {
    if (!heap_head || size == 0) {
        return NULL;
    }

    uint32_t needed = align_up((uint32_t)size);
    struct heap_block *block = heap_head;

    while (block) {
        if (block->free && block->size >= needed) {
            split_block(block, needed);
            block->free = 0;
            heap_used += block->size;
            return (void *)(block + 1);
        }
        block = block->next;
    }

    return NULL;
}

static void coalesce(struct heap_block *block) {
    if (block->next && block->next->free) {
        block->size += sizeof(struct heap_block) + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    if (block->prev && block->prev->free) {
        block->prev->size += sizeof(struct heap_block) + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

void kfree(void *ptr) {
    if (!ptr) {
        return;
    }

    struct heap_block *block = ((struct heap_block *)ptr) - 1;
    if (block->free) {
        return;
    }

    block->free = 1;
    if (heap_used >= block->size) {
        heap_used -= block->size;
    } else {
        heap_used = 0;
    }
    coalesce(block);
}

uint32_t allocator_get_free_bytes(void) {
    uint32_t total = 0;
    struct heap_block *block = heap_head;

    while (block) {
        if (block->free) {
            total += block->size;
        }
        block = block->next;
    }

    return total;
}

uint32_t allocator_get_used_bytes(void) {
    return heap_used;
}
