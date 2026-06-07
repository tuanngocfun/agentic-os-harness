#include "paging.h"
#include "frame.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define PAGES_PER_TABLE 1024
#define TABLES_PER_DIR 1024
#define PAGE_DIRECTORY_PHYSICAL 0x00100000
#define FIRST_PAGE_TABLE_PHYSICAL 0x00101000
#define IDENTITY_MAPPED_LIMIT 0x00400000

static uint32_t *const page_directory = (uint32_t *)PAGE_DIRECTORY_PHYSICAL;
static uint32_t *const first_page_table = (uint32_t *)FIRST_PAGE_TABLE_PHYSICAL;
static uint32_t *active_page_directory = (uint32_t *)PAGE_DIRECTORY_PHYSICAL;

static inline void load_page_directory(uint32_t *pd) {
    asm volatile("mov %0, %%cr3" : : "r"((uint32_t)pd));
}

static inline void enable_paging(void) {
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    cr0 |= 0x00010000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

static inline void invlpg(uint32_t addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static uint32_t *allocator_alloc_page_table(void) {
    uint32_t physical = paging_alloc_frame();
    if (!physical) return NULL;
    uint32_t *table = (uint32_t *)physical;
    for (int i = 0; i < 1024; i++) {
        table[i] = 0x02;
    }
    return table;
}

void paging_init(void) {
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x02;
    }

    for (int i = 0; i < 1024; i++) {
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    page_directory[0] = ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_WRITABLE;
    active_page_directory = page_directory;

    load_page_directory(page_directory);
    enable_paging();
}

void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    paging_map_page_in_directory((uint32_t)active_page_directory, virtual_addr, physical_addr, flags);
}

void paging_map_page_in_directory(uint32_t cr3, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t *directory = (uint32_t *)(cr3 & 0xFFFFF000);
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    uint32_t *page_table;
    if (!(directory[dir_index] & PAGE_PRESENT)) {
        page_table = allocator_alloc_page_table();
        if (!page_table) return;
        directory[dir_index] = ((uint32_t)page_table) | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else if (flags & PAGE_USER) {
        directory[dir_index] |= PAGE_USER;
    }

    page_table = (uint32_t *)(directory[dir_index] & 0xFFFFF000);
    page_table[table_index] = (physical_addr & 0xFFFFF000) | (flags & 0x07) | PAGE_PRESENT;

    invlpg(virtual_addr);
}

void paging_unmap_page(uint32_t virtual_addr) {
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    if (!(active_page_directory[dir_index] & PAGE_PRESENT)) {
        return;
    }

    uint32_t *page_table = (uint32_t *)(active_page_directory[dir_index] & 0xFFFFF000);
    page_table[table_index] = 0x02;

    invlpg(virtual_addr);
}

int paging_is_mapped(uint32_t virtual_addr) {
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    if (!(active_page_directory[dir_index] & PAGE_PRESENT)) {
        return 0;
    }

    uint32_t *page_table = (uint32_t *)(active_page_directory[dir_index] & 0xFFFFF000);
    return (page_table[table_index] & PAGE_PRESENT) != 0;
}

uint32_t paging_get_page_table_addr(uint32_t virtual_addr) {
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    if (!(active_page_directory[dir_index] & PAGE_PRESENT)) {
        return 0;
    }
    return active_page_directory[dir_index] & 0xFFFFF000;
}

int paging_is_user_accessible(uint32_t virtual_addr) {
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    if ((active_page_directory[dir_index] & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) {
        return 0;
    }

    uint32_t *page_table = (uint32_t *)(active_page_directory[dir_index] & 0xFFFFF000);
    return (page_table[table_index] & (PAGE_PRESENT | PAGE_USER)) == (PAGE_PRESENT | PAGE_USER);
}

int paging_is_user_writable(uint32_t virtual_addr) {
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    if ((active_page_directory[dir_index] & (PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE)) !=
        (PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE)) {
        return 0;
    }

    uint32_t *page_table = (uint32_t *)(active_page_directory[dir_index] & 0xFFFFF000);
    return (page_table[table_index] & (PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE)) ==
           (PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
}

uint32_t paging_get_current_directory(void) {
    return (uint32_t)active_page_directory;
}

void paging_switch_directory(uint32_t cr3) {
    if (!cr3) {
        return;
    }
    active_page_directory = (uint32_t *)(cr3 & 0xFFFFF000);
    load_page_directory(active_page_directory);
}

uint32_t paging_alloc_frame(void) {
    return frame_alloc_below(IDENTITY_MAPPED_LIMIT);
}

uint32_t paging_create_address_space(void) {
    uint32_t *directory = allocator_alloc_page_table();
    if (!directory) return 0;

    uint32_t *low_table = allocator_alloc_page_table();
    if (!low_table) return 0;

    for (int i = 0; i < 1024; i++) {
        low_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    directory[0] = ((uint32_t)low_table) | PAGE_PRESENT | PAGE_WRITABLE;
    return (uint32_t)directory;
}
