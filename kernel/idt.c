#include "idt.h"
#include <stdint.h>
#include <stddef.h>

#define IDT_ENTRIES 256

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = base & 0xFFFF;
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

static void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

void isr_handler(uint32_t interrupt) {
    if (interrupt >= 40) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    pic_remap();

    extern void isr_stub_32(void);
    extern void isr_stub_33(void);
    idt_set_gate(32, (uint32_t)isr_stub_32, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)isr_stub_33, 0x08, 0x8E);

    asm volatile("lidt %0" : : "m"(idtp));
    asm volatile("sti");
}
