#include "e820.h"
#include "serial.h"
#include <stddef.h>

static struct e820_map memory_map;
static int initialized = 0;
static int available = 0;

void e820_init(void) {
    struct e820_handoff *handoff = (struct e820_handoff *)E820_HANDOFF_ADDR;
    struct e820_entry *entries = (struct e820_entry *)(E820_HANDOFF_ADDR + sizeof(struct e820_handoff));

    memory_map.count = 0;
    available = 0;

    if (handoff->magic != E820_MAGIC) {
        initialized = 1;
        return;
    }

    memory_map.count = handoff->count;

    // Limit to max entries
    if (memory_map.count > E820_MAX_ENTRIES) {
        memory_map.count = E820_MAX_ENTRIES;
    }

    // Copy entries to our structure
    for (int i = 0; i < memory_map.count; i++) {
        memory_map.entries[i] = entries[i];
    }

    available = memory_map.count > 0;
    initialized = 1;
}

struct e820_map *e820_get_map(void) {
    return &memory_map;
}

uint64_t e820_get_total_usable_memory(void) {
    if (!initialized) return 0;

    uint64_t total = 0;
    for (int i = 0; i < memory_map.count; i++) {
        if (memory_map.entries[i].type == E820_USABLE) {
            total += memory_map.entries[i].length;
        }
    }

    return total;
}

int e820_is_region_usable(uint64_t base, uint64_t length) {
    if (!initialized) return 0;
    if (length == 0) return 0;

    for (int i = 0; i < memory_map.count; i++) {
        struct e820_entry *e = &memory_map.entries[i];

        if (e->type != E820_USABLE) continue;

        // Check if region is within this usable region
        uint64_t end = base + length;
        uint64_t entry_end = e->base + e->length;
        if (end >= base && entry_end >= e->base && base >= e->base && end <= entry_end) {
            return 1;
        }
    }

    return 0;
}

int e820_is_available(void) {
    return initialized && available;
}

static const char *e820_type_to_string(uint32_t type) {
    switch (type) {
        case E820_USABLE: return "Usable";
        case E820_RESERVED: return "Reserved";
        case E820_ACPI_RECLAIMABLE: return "ACPI Reclaimable";
        case E820_ACPI_NVS: return "ACPI NVS";
        case E820_BAD: return "Bad";
        default: return "Unknown";
    }
}

void e820_print_map(void) {
    if (!initialized) {
        serial_puts("E820: Not initialized\n");
        return;
    }

    serial_puts("E820 Memory Map (");
    serial_put_uint32(memory_map.count);
    serial_puts(" entries):\n");

    for (int i = 0; i < memory_map.count; i++) {
        struct e820_entry *e = &memory_map.entries[i];

        serial_puts("  [");
        serial_put_uint32(i);
        serial_puts("] Base: 0x");
        serial_put_uint32((uint32_t)(e->base >> 32));
        serial_put_uint32((uint32_t)e->base);
        serial_puts(" Len: 0x");
        serial_put_uint32((uint32_t)(e->length >> 32));
        serial_put_uint32((uint32_t)e->length);
        serial_puts(" Type: ");
        serial_puts(e820_type_to_string(e->type));
        serial_puts("\n");
    }

    uint64_t total = e820_get_total_usable_memory();
    serial_puts("Total usable memory: ");
    serial_put_uint32((uint32_t)(total >> 20));  // MB
    serial_puts(" MB\n");
}
