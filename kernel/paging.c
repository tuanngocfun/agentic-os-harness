#include "paging.h"
#include "frame.h"
#include "serial.h"
#include "string.h"
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define PAGES_PER_TABLE 1024
#define TABLES_PER_DIR 1024
#define PAGE_DIRECTORY_PHYSICAL 0x00100000
#define FIRST_PAGE_TABLE_PHYSICAL 0x00101000
#define IDENTITY_MAPPED_LIMIT 0x00400000
#define SCRATCH_DIRECTORY_INDEX 1023
#define SCRATCH_SOURCE_VADDR 0xFFC00000u
#define SCRATCH_DEST_VADDR   0xFFC01000u

static uint32_t *const page_directory = (uint32_t *)PAGE_DIRECTORY_PHYSICAL;
static uint32_t *const first_page_table = (uint32_t *)FIRST_PAGE_TABLE_PHYSICAL;
static uint32_t *active_page_directory = (uint32_t *)PAGE_DIRECTORY_PHYSICAL;
static uint32_t *kernel_scratch_table;

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

static int copy_physical_page(uint32_t source, uint32_t destination) {
    if (!kernel_scratch_table ||
        (source & (PAGE_SIZE - 1)) != 0 ||
        (destination & (PAGE_SIZE - 1)) != 0) {
        return 0;
    }

    kernel_scratch_table[0] = source | PAGE_PRESENT | PAGE_WRITABLE;
    kernel_scratch_table[1] = destination | PAGE_PRESENT | PAGE_WRITABLE;
    invlpg(SCRATCH_SOURCE_VADDR);
    invlpg(SCRATCH_DEST_VADDR);

    memcpy((void *)SCRATCH_DEST_VADDR, (const void *)SCRATCH_SOURCE_VADDR, PAGE_SIZE);

    kernel_scratch_table[0] = PAGE_WRITABLE;
    kernel_scratch_table[1] = PAGE_WRITABLE;
    invlpg(SCRATCH_SOURCE_VADDR);
    invlpg(SCRATCH_DEST_VADDR);
    return 1;
}

void paging_init(void) {
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x02;
    }

    for (int i = 0; i < 1024; i++) {
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    page_directory[0] = ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_WRITABLE;
    kernel_scratch_table = allocator_alloc_page_table();
    if (kernel_scratch_table) {
        page_directory[SCRATCH_DIRECTORY_INDEX] =
            ((uint32_t)kernel_scratch_table) | PAGE_PRESENT | PAGE_WRITABLE;
    }
    active_page_directory = page_directory;

    load_page_directory(page_directory);
    enable_paging();
}

void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    (void)paging_map_page_in_directory((uint32_t)active_page_directory,
                                       virtual_addr, physical_addr, flags);
}

int paging_map_page_in_directory(uint32_t cr3, uint32_t virtual_addr,
                                 uint32_t physical_addr, uint32_t flags) {
    uint32_t *directory = (uint32_t *)(cr3 & 0xFFFFF000);
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;
    uint32_t *page_table;

    if (!cr3 || (physical_addr & (PAGE_SIZE - 1)) != 0) {
        return 0;
    }

    if (!(directory[dir_index] & PAGE_PRESENT)) {
        page_table = allocator_alloc_page_table();
        if (!page_table) {
            return 0;
        }
        directory[dir_index] = ((uint32_t)page_table) | PAGE_PRESENT |
                               PAGE_WRITABLE | (flags & PAGE_USER);
    } else if (flags & PAGE_USER) {
        directory[dir_index] |= PAGE_USER;
    }

    page_table = (uint32_t *)(directory[dir_index] & 0xFFFFF000);
    page_table[table_index] = (physical_addr & 0xFFFFF000) |
                              (flags & 0xFFF) | PAGE_PRESENT;

    if (directory == active_page_directory) {
        invlpg(virtual_addr);
    }
    return 1;
}

uint32_t paging_unmap_page_in_directory(uint32_t cr3, uint32_t virtual_addr) {
    uint32_t *directory = (uint32_t *)(cr3 & 0xFFFFF000);
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    if (!cr3 || !(directory[dir_index] & PAGE_PRESENT)) {
        return 0;
    }

    uint32_t *page_table = (uint32_t *)(directory[dir_index] & 0xFFFFF000);
    uint32_t old_entry = page_table[table_index];
    page_table[table_index] = PAGE_WRITABLE;

    if (directory == active_page_directory) {
        invlpg(virtual_addr);
    }
    return old_entry & 0xFFFFF000;
}

void paging_unmap_page(uint32_t virtual_addr) {
    (void)paging_unmap_page_in_directory((uint32_t)active_page_directory,
                                         virtual_addr);
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

uint32_t paging_get_physical_address_in_directory(uint32_t cr3,
                                                  uint32_t virtual_addr) {
    uint32_t *directory = (uint32_t *)(cr3 & 0xFFFFF000);
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    if (!cr3 || !(directory[dir_index] & PAGE_PRESENT)) {
        return 0;
    }

    uint32_t *page_table = (uint32_t *)(directory[dir_index] & 0xFFFFF000);
    if (!(page_table[table_index] & PAGE_PRESENT)) {
        return 0;
    }

    return (page_table[table_index] & 0xFFFFF000) | (virtual_addr & 0xFFF);
}

uint32_t paging_get_page_flags_in_directory(uint32_t cr3,
                                             uint32_t virtual_addr) {
    uint32_t *directory = (uint32_t *)(cr3 & 0xFFFFF000);
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;

    if (!cr3 || !(directory[dir_index] & PAGE_PRESENT)) {
        return 0;
    }

    uint32_t *page_table = (uint32_t *)(directory[dir_index] & 0xFFFFF000);
    return page_table[table_index] & 0xFFF;
}

uint32_t paging_get_physical_address(uint32_t virtual_addr) {
    return paging_get_physical_address_in_directory(
        (uint32_t)active_page_directory, virtual_addr);
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

        if (table == first_page_table ||
            (i != 0 && !(directory[i] & PAGE_USER))) {
            continue;
        }

        for (int j = 0; j < 1024; j++) {
            if ((table[j] & (PAGE_PRESENT | PAGE_USER)) ==
                (PAGE_PRESENT | PAGE_USER)) {
                frame_free(table[j] & 0xFFFFF000);
                table[j] = 0x02;
            }
        }
        frame_free((uint32_t)table);
        directory[i] = 0x02;
    }

    frame_free((uint32_t)directory);
}

static void paging_destroy_clone_skeleton(uint32_t *directory) {
    if (!directory) {
        return;
    }

    for (int i = 0; i < TABLES_PER_DIR; i++) {
        if (!(directory[i] & PAGE_PRESENT)) {
            continue;
        }
        if (i != 0 && !(directory[i] & PAGE_USER)) {
            continue;
        }

        frame_release(directory[i] & 0xFFFFF000);
        directory[i] = PAGE_WRITABLE;
    }
    frame_release((uint32_t)directory);
}

static void paging_destroy_clone_with_refs(uint32_t *directory) {
    if (!directory) {
        return;
    }

    for (int i = 0; i < TABLES_PER_DIR; i++) {
        if (!(directory[i] & PAGE_PRESENT)) {
            continue;
        }
        if (i != 0 && !(directory[i] & PAGE_USER)) {
            continue;
        }

        uint32_t *table = (uint32_t *)(directory[i] & 0xFFFFF000);
        for (int j = 0; j < PAGES_PER_TABLE; j++) {
            if ((table[j] & (PAGE_PRESENT | PAGE_USER)) ==
                (PAGE_PRESENT | PAGE_USER)) {
                frame_release(table[j] & 0xFFFFF000);
                table[j] = PAGE_WRITABLE;
            }
        }
        frame_release((uint32_t)table);
        directory[i] = PAGE_WRITABLE;
    }
    frame_release((uint32_t)directory);
}

uint32_t paging_clone_directory(uint32_t src_cr3) {
    uint32_t *src_dir = (uint32_t *)(src_cr3 & 0xFFFFF000);
    uint32_t *new_dir;

    if (!src_cr3 || !kernel_scratch_table) {
        return 0;
    }

    new_dir = allocator_alloc_page_table();
    if (!new_dir) {
        return 0;
    }

    /* Phase 1: allocate every child table and validate source ownership. */
    for (int i = 0; i < TABLES_PER_DIR; i++) {
        if (!(src_dir[i] & PAGE_PRESENT)) {
            continue;
        }
        if (i != 0 && !(src_dir[i] & PAGE_USER)) {
            new_dir[i] = src_dir[i];
            continue;
        }

        uint32_t *src_table = (uint32_t *)(src_dir[i] & 0xFFFFF000);
        uint32_t *new_table = allocator_alloc_page_table();
        if (!new_table) {
            paging_destroy_clone_skeleton(new_dir);
            return 0;
        }
        new_dir[i] = ((uint32_t)new_table & 0xFFFFF000) |
                     (src_dir[i] & 0xFFF);

        for (int j = 0; j < PAGES_PER_TABLE; j++) {
            if ((src_table[j] & (PAGE_PRESENT | PAGE_USER)) ==
                (PAGE_PRESENT | PAGE_USER)) {
                uint32_t refs = frame_get_refcount(src_table[j] & 0xFFFFF000);
                if (refs == 0 || refs == 0xFFFFu) {
                    paging_destroy_clone_skeleton(new_dir);
                    return 0;
                }
            }
        }
    }

    /* Phase 2a: retain all shared frames while the parent is untouched. */
    for (int i = 0; i < TABLES_PER_DIR; i++) {
        if (!(new_dir[i] & PAGE_PRESENT) ||
            (i != 0 && !(new_dir[i] & PAGE_USER))) {
            continue;
        }

        uint32_t *src_table = (uint32_t *)(src_dir[i] & 0xFFFFF000);
        uint32_t *new_table = (uint32_t *)(new_dir[i] & 0xFFFFF000);
        for (int j = 0; j < PAGES_PER_TABLE; j++) {
            if (!(src_table[j] & PAGE_PRESENT)) {
                continue;
            }
            if (!(src_table[j] & PAGE_USER)) {
                new_table[j] = src_table[j];
                continue;
            }
            if (!frame_retain(src_table[j] & 0xFFFFF000)) {
                paging_destroy_clone_with_refs(new_dir);
                return 0;
            }
            new_table[j] = src_table[j];
        }
    }

    /* Phase 2b: commit writable user mappings as read-only COW. */
    for (int i = 0; i < TABLES_PER_DIR; i++) {
        if (!(new_dir[i] & PAGE_PRESENT) ||
            (i != 0 && !(new_dir[i] & PAGE_USER))) {
            continue;
        }

        uint32_t *src_table = (uint32_t *)(src_dir[i] & 0xFFFFF000);
        uint32_t *new_table = (uint32_t *)(new_dir[i] & 0xFFFFF000);
        for (int j = 0; j < PAGES_PER_TABLE; j++) {
            uint32_t entry = src_table[j];
            if ((entry & (PAGE_PRESENT | PAGE_USER)) !=
                (PAGE_PRESENT | PAGE_USER)) {
                continue;
            }
            if (entry & (PAGE_WRITABLE | PAGE_COW)) {
                entry = (entry & ~PAGE_WRITABLE) | PAGE_COW;
                src_table[j] = entry;
                new_table[j] = entry;
            }
        }
    }

    if (src_dir == active_page_directory) {
        load_page_directory(active_page_directory);
    }
    return (uint32_t)new_dir;
}

int paging_resolve_cow_fault(uint32_t cr3, uint32_t virtual_addr) {
    uint32_t *directory = (uint32_t *)(cr3 & 0xFFFFF000);
    uint32_t dir_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;
    uint32_t *table;
    uint32_t entry;
    uint32_t old_phys;
    uint32_t refs;

    if (!cr3 || !(directory[dir_index] & PAGE_PRESENT)) {
        return 0;
    }

    table = (uint32_t *)(directory[dir_index] & 0xFFFFF000);
    entry = table[table_index];
    if ((entry & (PAGE_PRESENT | PAGE_USER | PAGE_COW)) !=
            (PAGE_PRESENT | PAGE_USER | PAGE_COW) ||
        (entry & PAGE_WRITABLE)) {
        return 0;
    }

    old_phys = entry & 0xFFFFF000;
    refs = frame_get_refcount(old_phys);
    if (refs == 0) {
        return 0;
    }

    if (refs == 1) {
        table[table_index] = (entry | PAGE_WRITABLE) & ~PAGE_COW;
        if (directory == active_page_directory) {
            invlpg(virtual_addr);
        }
        return 1;
    }

    uint32_t new_phys = frame_alloc();
    if (!new_phys) {
        return 0;
    }
    if (!copy_physical_page(old_phys, new_phys)) {
        frame_release(new_phys);
        return 0;
    }

    table[table_index] = new_phys |
                         (((entry & 0xFFF) | PAGE_WRITABLE) & ~PAGE_COW);
    if (directory == active_page_directory) {
        invlpg(virtual_addr);
    }
    frame_release(old_phys);
    return 1;
}
