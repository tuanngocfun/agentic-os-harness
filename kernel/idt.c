#include "idt.h"
#ifdef ENABLE_PAGING_SELFTEST
#include "paging.h"
#endif
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
    outb(0x21, 0xFC);  // IRQ0 (timer) masked until preemptive scheduling ready
    outb(0xA1, 0xFF);
}

void isr_handler(uint32_t interrupt) {
    if (interrupt >= 40) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

static void panic_putc(char c) {
    while (!(inb(0x3F8 + 5) & 0x20));
    outb(0x3F8, c);
}

static void panic_puts(const char *str) {
    while (*str) {
        panic_putc(*str++);
    }
}

static void panic_puthex(uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        panic_putc(hex[(val >> i) & 0xF]);
    }
}

#ifdef ENABLE_PAGING_SELFTEST
volatile int page_fault_expected = 0;
volatile int page_fault_caught = 0;
volatile uint32_t page_fault_addr = 0;
volatile uint32_t page_fault_repair_virtual = 0;
volatile uint32_t page_fault_repair_physical = 0;
volatile uint32_t page_fault_repair_flags = 0;
#endif

void exception_handler(uint32_t vector, uint32_t error_code) {
#ifdef ENABLE_PAGING_SELFTEST
    if (vector == 14 && page_fault_expected && page_fault_caught == 0) {
        asm volatile("mov %%cr2, %0" : "=r"(page_fault_addr));
        if (page_fault_addr == page_fault_repair_virtual) {
            paging_map_page(page_fault_repair_virtual,
                            page_fault_repair_physical,
                            page_fault_repair_flags);
            page_fault_expected = 0;
        }
        page_fault_caught = 1;
        if (page_fault_addr == page_fault_repair_virtual) {
            return;
        }
    }
#endif

#ifdef ENABLE_USERMODE_SELFTEST
    if (vector == 14) {
        uint32_t fault_addr;
        asm volatile("mov %%cr2, %0" : "=r"(fault_addr));
        if (fault_addr == 0x00700000 && (error_code & 0x05) == 0x05) {
            panic_puts("PAGING_USER_SUPERVISOR_FAULT_OK\n");
        }
    }
#endif

    panic_puts("KERNEL_PANIC:0x");
    panic_puthex(vector);
    panic_puts(":0x");
    panic_puthex(error_code);

    if (vector == 14) {
        uint32_t fault_addr;
        asm volatile("mov %%cr2, %0" : "=r"(fault_addr));
        panic_puts(":0x");
        panic_puthex(fault_addr);
    }

    panic_puts("\n");

    while (1) {
        asm volatile("cli; hlt");
    }
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    pic_remap();

    extern void isr_stub_0(void);
    extern void isr_stub_6(void);
    extern void isr_stub_13(void);
    extern void isr_stub_14(void);
    extern void isr_stub_32(void);
    extern void isr_stub_33(void);

    idt_set_gate(0, (uint32_t)isr_stub_0, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr_stub_6, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr_stub_13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr_stub_14, 0x08, 0x8E);
    idt_set_gate(32, (uint32_t)isr_stub_32, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)isr_stub_33, 0x08, 0x8E);

    asm volatile("lidt %0" : : "m"(idtp));
    asm volatile("sti");
}
