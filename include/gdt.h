#ifndef GDT_H
#define GDT_H

#include <stdint.h>

void gdt_init(void);
void gdt_set_gate(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

#endif
