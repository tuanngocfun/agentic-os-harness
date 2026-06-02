#include "paging.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define PAGES_PER_TABLE 1024
#define TABLES_PER_DIR 1024
#define TOTAL_PAGES 1024

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t first_page_table[1024] __attribute__((aligned(4096)));

static uint8_t page_bitmap[TOTAL_PAGES / 8];

static inline void load_page_directory(uint32_t *pd) {
    asm volatile("mov %0, %%cr3" : : "r"((uint32_t)pd));
}

static inline void enable_paging(void) {
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

static inline void invlpg(uint32_t addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static void bitmap_set(int page) {
    page_bitmap[page / 8] |= (1 << (page % 8));
}

static void bitmap_clear(int page) {
    page_bitmap[page / 8] &= ~(1 << (page % 8));
}

static int bitmap_test(int page) {
    return (page_bitmap[page / 8] >> (page % 8)) & 1;
}

static int allocator_find_free(void) {
    for (int i = 0; i < TOTAL_PAGES; i++) {
        if (!bitmap_test(i)) {
            return i;
        }
    }
    return -1;
}

static uint32_t *allocator_alloc_page_table(void) {
    int page = allocator_find_free();
    if (page < 0) return NULL;
    bitmap_set(page);
    uint32_t *table = (uint32_t *)(page * PAGE_SIZE);
    for (int i = 0; i < 1024; i++) {
        table[i] = 0x02;
    }
    return table;
}

void paging_init(void) {
    for (int i = 0; i < TOTAL_PAGES / 8; i++) {
        page_bitmap[i] = 0;
    }

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x02;
    }

    for (int i = 0; i < 1024; i++) {
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    page_directory[0] = ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_WRITABLE;

    for (int i = 0; i < 4; i++) {
        bitmap_set(i);
    }

    load_page_directory(page_directory);
    enable_paging();
}

void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    uint32_t *page_table;
    if (!(page_directory[dir_index] & PAGE_PRESENT)) {
        page_table = allocator_alloc_page_table();
        if (!page_table) return;
        page_directory[dir_index] = ((uint32_t)page_table) | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }

    page_table = (uint32_t *)(page_directory[dir_index] & 0xFFFFF000);
    page_table[table_index] = (physical_addr & 0xFFFFF000) | (flags & 0x07) | PAGE_PRESENT;

    invlpg(virtual_addr);
}

void paging_unmap_page(uint32_t virtual_addr) {
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    if (!(page_directory[dir_index] & PAGE_PRESENT)) {
        return;
    }

    uint32_t *page_table = (uint32_t *)(page_directory[dir_index] & 0xFFFFF000);
    page_table[table_index] = 0x02;

    invlpg(virtual_addr);
}

uint32_t paging_get_page_table_addr(uint32_t virtual_addr) {
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    if (!(page_directory[dir_index] & PAGE_PRESENT)) {
        return 0;
    }
    return page_directory[dir_index] & 0xFFFFF000;
}
