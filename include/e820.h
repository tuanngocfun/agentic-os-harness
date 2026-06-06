#ifndef E820_H
#define E820_H

#include <stdint.h>

#define E820_USABLE 1
#define E820_RESERVED 2
#define E820_ACPI_RECLAIMABLE 3
#define E820_ACPI_NVS 4
#define E820_BAD 5

#define E820_MAX_ENTRIES 32
#define E820_HANDOFF_ADDR 0x00080000
#define E820_MAGIC 0x30323845
#define E820_ENTRY_SIZE 24

struct e820_handoff {
    uint32_t magic;
    uint16_t count;
    uint16_t reserved;
} __attribute__((packed));

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
} __attribute__((packed));

struct e820_map {
    uint16_t count;
    struct e820_entry entries[E820_MAX_ENTRIES];
};

void e820_init(void);
struct e820_map *e820_get_map(void);
uint64_t e820_get_total_usable_memory(void);
int e820_is_region_usable(uint64_t base, uint64_t length);
int e820_is_available(void);
void e820_print_map(void);

#endif
