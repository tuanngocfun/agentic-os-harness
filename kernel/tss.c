#include "tss.h"
#include <stdint.h>
#include <string.h>

static struct tss_entry tss;

extern void gdt_set_gate(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

void tss_init(void) {
    memset(&tss, 0, sizeof(tss));

    tss.ss0 = 0x10;
    tss.esp0 = 0x90000;
    tss.cs = 0x08 | 0x03;
    tss.ss = 0x10 | 0x03;
    tss.ds = 0x10 | 0x03;
    tss.es = 0x10 | 0x03;
    tss.fs = 0x10 | 0x03;
    tss.gs = 0x10 | 0x03;
    tss.iomap_base = sizeof(tss);

    gdt_set_gate(5, (uint32_t)&tss, sizeof(tss), 0xE9, 0x00);

    asm volatile("ltr %%ax" : : "a"((uint16_t)0x28));
}

void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}
