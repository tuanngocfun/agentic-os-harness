#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#ifdef ENABLE_PAGING_SELFTEST
extern volatile int page_fault_expected;
extern volatile int page_fault_caught;
extern volatile uint32_t page_fault_addr;
extern volatile uint32_t page_fault_repair_virtual;
extern volatile uint32_t page_fault_repair_physical;
extern volatile uint32_t page_fault_repair_flags;
#endif

#endif
