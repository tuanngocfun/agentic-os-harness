#include "kernel.h"
#include "serial.h"
#include "vga.h"

void kernel_main(void) {
    serial_init();
    serial_puts("KERNEL_INIT_OK\n");

    vga_init();
    vga_puts("Kernel initialized successfully!\n");

    serial_puts("SHELL_READY\n");

    while (1) {
        asm volatile("hlt");
    }
}
