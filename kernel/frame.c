#include "frame.h"
#include "e820.h"
#include "memory.h"
#include <stdint.h>

#define MAX_TRACKED_MEMORY 0x20000000u
#define MAX_FRAMES (MAX_TRACKED_MEMORY / FRAME_SIZE)
#define RESERVED_LOW_END 0x00300000u

static uint8_t frame_bitmap[MAX_FRAMES / 8];
static uint32_t total_frames = 0;
static uint32_t used_frames = 0;
static int frame_initialized = 0;

extern uint8_t __kernel_end;

static uint32_t align_down(uint32_t value) {
    return value & ~(FRAME_SIZE - 1);
}

static uint32_t align_up(uint32_t value) {
    return (value + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
}

static void bitmap_set(uint32_t frame) {
    frame_bitmap[frame / 8] |= (uint8_t)(1u << (frame % 8));
}

static void bitmap_clear(uint32_t frame) {
    frame_bitmap[frame / 8] &= (uint8_t)~(1u << (frame % 8));
}

static int bitmap_test(uint32_t frame) {
    return (frame_bitmap[frame / 8] >> (frame % 8)) & 1u;
}

static void mark_allocated(uint32_t start_addr, uint32_t end_addr) {
    uint32_t start = align_down(start_addr) / FRAME_SIZE;
    uint32_t end = align_up(end_addr) / FRAME_SIZE;
    if (end > total_frames) end = total_frames;

    for (uint32_t frame = start; frame < end; frame++) {
        if (!bitmap_test(frame)) {
            bitmap_set(frame);
            used_frames++;
        }
    }
}

static void mark_free(uint32_t start_addr, uint32_t end_addr) {
    uint32_t start = align_up(start_addr) / FRAME_SIZE;
    uint32_t end = align_down(end_addr) / FRAME_SIZE;
    if (end > total_frames) end = total_frames;

    for (uint32_t frame = start; frame < end; frame++) {
        if (bitmap_test(frame)) {
            bitmap_clear(frame);
            if (used_frames > 0) {
                used_frames--;
            }
        }
    }
}

static uint32_t highest_memory_from_e820(void) {
    struct e820_map *map = e820_get_map();
    uint32_t highest = 0;

    for (int i = 0; i < map->count; i++) {
        if (map->entries[i].type != E820_USABLE) {
            continue;
        }

        uint64_t end64 = map->entries[i].base + map->entries[i].length;
        uint32_t end = end64 > MAX_TRACKED_MEMORY ? MAX_TRACKED_MEMORY : (uint32_t)end64;
        if (end > highest) {
            highest = end;
        }
    }

    return highest;
}

void frame_init(void) {
    uint32_t highest = highest_memory_from_e820();

    if (highest < (16 * 1024 * 1024)) {
        uint32_t total_kb = memory_get_total_kb();
        highest = total_kb * 1024;
        if (highest > MAX_TRACKED_MEMORY) {
            highest = MAX_TRACKED_MEMORY;
        }
    }

    total_frames = highest / FRAME_SIZE;
    if (total_frames > MAX_FRAMES) {
        total_frames = MAX_FRAMES;
    }

    for (uint32_t i = 0; i < sizeof(frame_bitmap); i++) {
        frame_bitmap[i] = 0xFF;
    }
    used_frames = total_frames;

    if (e820_is_available()) {
        struct e820_map *map = e820_get_map();
        for (int i = 0; i < map->count; i++) {
            if (map->entries[i].type != E820_USABLE) {
                continue;
            }
            uint64_t base64 = map->entries[i].base;
            uint64_t end64 = map->entries[i].base + map->entries[i].length;
            if (base64 > MAX_TRACKED_MEMORY) {
                continue;
            }
            uint32_t base = (uint32_t)base64;
            uint32_t end = end64 > MAX_TRACKED_MEMORY ? MAX_TRACKED_MEMORY : (uint32_t)end64;
            mark_free(base, end);
        }
    } else {
        mark_free(0, total_frames * FRAME_SIZE);
    }

    mark_allocated(0, RESERVED_LOW_END);
    mark_allocated(0x00080000, 0x00081000);
    mark_allocated(0x00100000, 0x00102000);
    mark_allocated(0x00200000, 0x00300000);
    mark_allocated(0x00001000, align_up((uint32_t)&__kernel_end));

    frame_initialized = 1;
}

uint32_t frame_alloc_below(uint32_t limit) {
    if (!frame_initialized || limit == 0) {
        return 0;
    }

    uint32_t max_frame = limit / FRAME_SIZE;
    if (max_frame > total_frames) {
        max_frame = total_frames;
    }

    uint32_t start_frame = RESERVED_LOW_END / FRAME_SIZE;
    for (uint32_t frame = start_frame; frame < max_frame; frame++) {
        if (!bitmap_test(frame)) {
            bitmap_set(frame);
            used_frames++;
            return frame * FRAME_SIZE;
        }
    }

    return 0;
}

uint32_t frame_alloc(void) {
    return frame_alloc_below(total_frames * FRAME_SIZE);
}

void frame_free(uint32_t phys_addr) {
    if (!frame_initialized || (phys_addr & (FRAME_SIZE - 1)) != 0) {
        return;
    }

    uint32_t frame = phys_addr / FRAME_SIZE;
    if (frame >= total_frames || phys_addr < RESERVED_LOW_END) {
        return;
    }

    if (bitmap_test(frame)) {
        bitmap_clear(frame);
        if (used_frames > 0) {
            used_frames--;
        }
    }
}

int frame_is_allocated(uint32_t phys_addr) {
    uint32_t frame = phys_addr / FRAME_SIZE;
    if (!frame_initialized || frame >= total_frames) {
        return 1;
    }
    return bitmap_test(frame);
}

uint32_t frame_get_total_count(void) {
    return total_frames;
}

uint32_t frame_get_free_count(void) {
    if (used_frames > total_frames) {
        return 0;
    }
    return total_frames - used_frames;
}

uint32_t frame_get_used_count(void) {
    return used_frames;
}
