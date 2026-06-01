#include "shell.h"
#include "keyboard.h"
#include "vga.h"
#include "serial.h"
#include "timer.h"
#include "memory.h"
#include "string.h"

#define CMD_BUFFER_SIZE 128

static char cmd_buffer[CMD_BUFFER_SIZE];
static int cmd_pos = 0;

static void shell_prompt(void) {
    vga_puts("> ");
}

static void print_uint32(uint32_t val) {
    char buf[11];
    int i = 0;
    if (val == 0) {
        vga_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) {
        vga_putc(buf[--i]);
    }
}

static void shell_execute(const char *cmd) {
    if (cmd[0] == '\0') {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        vga_puts("Available commands:\n");
        vga_puts("  help    - show this message\n");
        vga_puts("  clear   - clear screen\n");
        vga_puts("  echo    - echo text\n");
        vga_puts("  uptime  - show system uptime\n");
        vga_puts("  mem     - show memory info\n");
        vga_puts("  version - show OS version\n");
        vga_puts("  reboot  - reboot (triple fault)\n");
    } else if (strcmp(cmd, "clear") == 0) {
        vga_clear();
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        vga_puts(cmd + 5);
        vga_puts("\n");
    } else if (strcmp(cmd, "uptime") == 0) {
        uint32_t ticks = timer_get_ticks();
        uint32_t seconds = timer_get_seconds();
        vga_puts("Uptime: ");
        print_uint32(seconds);
        vga_puts(" seconds (");
        print_uint32(ticks);
        vga_puts(" ticks)\n");
    } else if (strcmp(cmd, "mem") == 0) {
        uint32_t total_kb = memory_get_total_kb();
        vga_puts("Memory: ");
        print_uint32(total_kb / 1024);
        vga_puts(" MB total\n");
    } else if (strcmp(cmd, "version") == 0) {
        vga_puts("x86 Bare Metal OS v0.1.0\n");
        vga_puts("Built with i686-elf-gcc 13.2.0\n");
    } else if (strcmp(cmd, "reboot") == 0) {
        vga_puts("Rebooting...\n");
        asm volatile("int $0x03");
    } else {
        vga_puts("Unknown command: ");
        vga_puts(cmd);
        vga_puts("\nType 'help' for available commands.\n");
    }
}

void shell_init(void) {
    cmd_pos = 0;
    memset(cmd_buffer, 0, CMD_BUFFER_SIZE);
}

void shell_run(void) {
    shell_prompt();

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            vga_putc('\n');
            cmd_buffer[cmd_pos] = '\0';
            shell_execute(cmd_buffer);
            cmd_pos = 0;
            memset(cmd_buffer, 0, CMD_BUFFER_SIZE);
            shell_prompt();
        } else if (c == '\b') {
            if (cmd_pos > 0) {
                cmd_pos--;
                cmd_buffer[cmd_pos] = '\0';
                vga_putc('\b');
                vga_putc(' ');
                vga_putc('\b');
            }
        } else if (c >= 32 && c < 127) {
            if (cmd_pos < CMD_BUFFER_SIZE - 1) {
                cmd_buffer[cmd_pos++] = c;
                vga_putc(c);
            }
        }
    }
}
