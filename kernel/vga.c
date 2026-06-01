#include "vga.h"
#include <stddef.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

enum vga_color {
    VGA_BLACK = 0,
    VGA_LIGHT_GREY = 7,
    VGA_WHITE = 15,
};

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static uint8_t vga_color(enum vga_color fg, enum vga_color bg) {
    return fg | (bg << 4);
}

static size_t vga_row;
static size_t vga_col;
static uint8_t vga_attr;

void vga_init(void) {
    vga_row = 0;
    vga_col = 0;
    vga_attr = vga_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_BUFFER[i] = vga_entry(' ', vga_attr);
    }
    vga_row = 0;
    vga_col = 0;
}

static void vga_scroll(void) {
    for (size_t i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
    }
    for (size_t i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_BUFFER[i] = vga_entry(' ', vga_attr);
    }
    vga_row = VGA_HEIGHT - 1;
    vga_col = 0;
}

void vga_putc(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_scroll();
        }
        return;
    }

    if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
        } else if (vga_row > 0) {
            vga_row--;
            vga_col = VGA_WIDTH - 1;
        }
        return;
    }

    VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_attr);
    vga_col++;
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_scroll();
        }
    }
}

void vga_puts(const char *str) {
    while (*str) {
        vga_putc(*str++);
    }
}
