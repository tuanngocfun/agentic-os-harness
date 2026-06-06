#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void paging_init(void);
void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
void paging_unmap_page(uint32_t virtual_addr);
int paging_is_mapped(uint32_t virtual_addr);
int paging_is_user_accessible(uint32_t virtual_addr);
uint32_t paging_get_current_directory(void);
void paging_switch_directory(uint32_t cr3);
uint32_t paging_create_address_space(void);
void paging_map_page_in_directory(uint32_t cr3, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
uint32_t paging_alloc_frame(void);

#define PAGE_PRESENT    0x01
#define PAGE_WRITABLE   0x02
#define PAGE_USER       0x04

#endif
