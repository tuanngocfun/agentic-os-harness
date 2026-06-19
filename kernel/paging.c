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

uint32_t paging_get_physical_address(uint32_t virtual_addr) {
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    if (!(active_page_directory[dir_index] & PAGE_PRESENT)) {
        return 0;
    }

    uint32_t *page_table = (uint32_t *)(active_page_directory[dir_index] & 0xFFFFF000);
    if (!(page_table[table_index] & PAGE_PRESENT)) {
        return 0;
    }

    return (page_table[table_index] & 0xFFFFF000) | (virtual_addr & 0xFFF);
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
    if (!low_table) {
        frame_free((uint32_t)directory);
        return 0;
    }

    for (int i = 0; i < 1024; i++) {
        low_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    directory[0] = ((uint32_t)low_table) | PAGE_PRESENT | PAGE_WRITABLE;

    // Share supervisor-only runtime mappings such as the ramdisk and kernel
    // data/stack tables. User mappings are intentionally not inherited.
    for (int i = 1; i < 1024; i++) {
        if ((active_page_directory[i] & PAGE_PRESENT) &&
            !(active_page_directory[i] & PAGE_USER)) {
            directory[i] = active_page_directory[i] & ~PAGE_USER;
        }
    }

    return (uint32_t)directory;
}

void paging_destroy_address_space(uint32_t cr3) {
    uint32_t *directory = (uint32_t *)(cr3 & 0xFFFFF000);

    if (!cr3 || directory == page_directory || directory == active_page_directory) {
        return;
    }

    for (int i = 0; i < 1024; i++) {
        if (!(directory[i] & PAGE_PRESENT)) {
            continue;
        }

        uint32_t *table = (uint32_t *)(directory[i] & 0xFFFFF000);

        if (i == 0) {
            if (table != first_page_table) {
                frame_free((uint32_t)table);
            }
            directory[i] = 0x02;
            continue;
        }

        if (!(directory[i] & PAGE_USER)) {
            continue;
        }

        for (int j = 0; j < 1024; j++) {
            if (table[j] & PAGE_PRESENT) {
                frame_free(table[j] & 0xFFFFF000);
                table[j] = 0x02;
            }
        }
        frame_free((uint32_t)table);
        directory[i] = 0x02;
    }

    frame_free((uint32_t)directory);
}

static void paging_destroy_cloned_directory(uint32_t *directory) {
    if (!directory) {
        return;
    }

    for (int i = 1; i < 1024; i++) {
        if (!(directory[i] & PAGE_PRESENT)) {
            continue;
        }

        uint32_t *table = (uint32_t *)(directory[i] & 0xFFFFF000);
        for (int j = 0; j < 1024; j++) {
            if (table[j] & PAGE_PRESENT) {
                frame_free(table[j] & 0xFFFFF000);
            }
        }
        frame_free((uint32_t)table);
        directory[i] = 0x02;
    }

    frame_free((uint32_t)directory);
}

uint32_t paging_clone_directory(uint32_t src_cr3) {
    uint32_t *src_dir = (uint32_t *)(src_cr3 & 0xFFFFF000);

    // Allocate new page directory
    uint32_t *new_dir = allocator_alloc_page_table();
    if (!new_dir) {
        return 0;
    }

    // Copy directory entries
    for (int i = 0; i < 1024; i++) {
        if (!(src_dir[i] & PAGE_PRESENT)) {
            new_dir[i] = 0x02;  // Not present
            continue;
        }

        // For kernel pages (first entry), share the same page table
        if (i == 0) {
            new_dir[i] = src_dir[i];
            continue;
        }

        // For user pages, clone page tables
        uint32_t *src_table = (uint32_t *)(src_dir[i] & 0xFFFFF000);

        // Allocate new page table
        uint32_t *new_table = allocator_alloc_page_table();
        if (!new_table) {
            paging_destroy_cloned_directory(new_dir);
            return 0;
        }

        // Link immediately so partial-failure cleanup can reclaim the table.
        new_dir[i] = ((uint32_t)new_table & 0xFFFFF000) | (src_dir[i] & 0xFFF);

        // Copy page table entries and physical pages
        for (int j = 0; j < 1024; j++) {
            if (!(src_table[j] & PAGE_PRESENT)) {
                new_table[j] = 0x02;
                continue;
            }

            // Allocate new physical page
            uint32_t new_page_phys = paging_alloc_frame();
            if (!new_page_phys) {
                paging_destroy_cloned_directory(new_dir);
                return 0;
            }

            // Copy page contents (4096 bytes = 1024 uint32_t)
            uint32_t *src_page = (uint32_t *)(src_table[j] & 0xFFFFF000);
            uint32_t *new_page = (uint32_t *)new_page_phys;

            for (int k = 0; k < 1024; k++) {
                new_page[k] = src_page[k];
            }

            // Set new page table entry with same flags
            new_table[j] = (new_page_phys & 0xFFFFF000) | (src_table[j] & 0xFFF);
        }
    }

    return (uint32_t)new_dir;
}
