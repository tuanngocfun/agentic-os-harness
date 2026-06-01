# Boot Markers — Serial Output Protocol

## Mục đích

Boot markers là các chuỗi text in ra **COM1 serial port** (`0x3F8`) để automated test harness verify boot progress mà không cần human intervention.

Quan trọng: `qemu-system-i386 -serial mon:stdio -display none` có thể capture COM1 serial output ra terminal cho human debug. Automated tests nên dùng `-serial file:build/serial.log -monitor none` để COM1 không bị trộn với QEMU monitor. Không mode nào capture text in bằng BIOS text output hoặc VGA text buffer.

## Protocol

### Marker Contract

| Marker | Ý nghĩa | Khi nào in | Bắt buộc? | Channel |
|---|---|---|---|---|
| `BOOT_OK` | Loader path đã tới protected mode sau khi BIOS disk read báo success; chưa chứng minh kernel identity nếu chưa verify magic/header | Sau protected-mode transition, trước jump/call kernel | Có | COM1 |
| `KERNEL_INIT_OK` | `kernel_main()` chạy và kernel serial driver hoạt động | Đầu `kernel_main()`, ngay sau `serial_init()` | Có | COM1 |
| `SHELL_READY` | Shell đang chờ input | Sau `shell_init()` | Có trong current shell phase; tùy chọn trước khi có shell | COM1 |
| `TESTS_PASS` | Internal integration tests pass | Sau khi chạy self-tests | Tùy chọn | COM1 |
| `BOOT_DISK_ERROR` | Bootloader không đọc được kernel từ disk | Trong bootloader disk error path | Failure marker; fail nếu xuất hiện | COM1 |
| `KERNEL_PANIC` | Kernel gặp lỗi nghiêm trọng | Trong panic handler trước khi halt | Failure marker; fail nếu xuất hiện | COM1 |

### Format

```
MARKER_NAME\n
```

- Không có prefix/suffix khác.
- Phải kết thúc bằng newline (`\n` hoặc `0x0A`).
- Có thể emit `\r\n` nếu terminal cần carriage return.
- Test script dùng exact marker names; đừng đổi tên nếu chưa update toàn bộ docs.

## Bootloader COM1 Output

`BOOT_OK` phải đi qua COM1 serial nếu automated test cần detect marker này. BIOS/VGA output có thể vẫn dùng cho debug mode, nhưng không được xem là source cho `build/serial.log`.

Real-mode setup phải init segment registers trước mọi memory/string/disk access:

```nasm
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
```

BIOS không đảm bảo `DS`, `ES`, hoặc `SS` đã trỏ tới segment bạn mong muốn. Nếu store `DL`, dùng `lodsb`, hoặc gọi `INT 13h` với `ES:BX` trước khi init segments, boot có thể fail không ổn định.

```nasm
[bits 16]

COM1 equ 0x3F8

serial_init_16:
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al
    mov dx, COM1 + 0
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al
    mov dx, COM1 + 4
    mov al, 0x0B
    out dx, al
    ret

serial_putc_16:
    push dx
    push ax
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    pop ax
    mov dx, COM1
    out dx, al
    pop dx
    ret

serial_puts_16:
    lodsb
    test al, al
    jz .done
    call serial_putc_16
    jmp serial_puts_16
.done:
    ret

; Phase 1: init serial in real mode.
    call serial_init_16

; ... load kernel, setup GDT, switch to protected mode ...

[bits 32]
    mov esi, MSG_BOOT_OK
    call serial_puts_32

; ... jump/call kernel entry ...

serial_putc_32:
    push edx
    push eax
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    pop eax
    mov dx, COM1
    out dx, al
    pop edx
    ret

serial_puts_32:
    lodsb
    test al, al
    jz .done
    call serial_putc_32
    jmp serial_puts_32
.done:
    ret

MSG_BOOT_OK: db 'BOOT_OK', 0x0A, 0
```

## Kernel Serial Driver

```c
#include "serial.h"

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static int serial_is_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c) {
    while (!serial_is_transmit_empty()) {
    }
    outb(COM1, c);
}

void serial_puts(const char *str) {
    while (*str) {
        serial_putc(*str++);
    }
}
```

## Serial Header (`serial.h`)

```c
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *str);

#endif
```

## Kernel Marker Placement

```c
#include "serial.h"
#include "vga.h"

void kernel_main(void) {
    serial_init();
    serial_puts("KERNEL_INIT_OK\n");

    vga_init();
    vga_puts("Kernel initialized successfully!\n");

    shell_init();
    serial_puts("SHELL_READY\n");

    shell_run();
}
```

## Boot Test Harness — Marker Detection

```bash
#!/bin/bash
# scripts/boot_test.sh

set -e

SERIAL_LOG="build/serial.log"
SERIAL_CLEAN="build/serial.clean.log"
QEMU_LOG="build/qemu.log"
TIMEOUT=30

mkdir -p build
: > "$SERIAL_LOG"
: > "$SERIAL_CLEAN"
: > "$QEMU_LOG"

set +e
timeout "$TIMEOUT" qemu-system-i386 \
  -drive file=build/os.img,format=raw \
  -m 512M \
  -serial file:"$SERIAL_LOG" \
  -monitor none \
  -nic none \
  -display none \
  -no-reboot \
  > "$QEMU_LOG" 2>&1
qemu_status=$?
set -e

case "$qemu_status" in
    124) ;;
    0) echo "[FAIL] qemu exited before timeout; possible shutdown or triple fault after markers"; exit 1 ;;
    *) echo "[FAIL] qemu exited with status $qemu_status"; exit 1 ;;
esac

tr -d '\r' < "$SERIAL_LOG" > "$SERIAL_CLEAN"

PASS=true

for marker in BOOT_OK KERNEL_INIT_OK; do
    if grep -Fxq "$marker" "$SERIAL_CLEAN"; then
        echo "[PASS] $marker"
    else
        echo "[FAIL] $marker"
        PASS=false
    fi
done

if grep -Fxq "KERNEL_PANIC" "$SERIAL_CLEAN"; then
    echo "[FAIL] KERNEL_PANIC"
    PASS=false
fi

if grep -Fxq "BOOT_DISK_ERROR" "$SERIAL_CLEAN"; then
    echo "[FAIL] BOOT_DISK_ERROR"
    PASS=false
fi

if [ "$PASS" = true ]; then
    exit 0
fi

exit 1
```

## Marker Ordering

```
Timeline:
    boot.asm starts
    ├─ initialize DS/ES/SS/SP
    ├─ init COM1 serial
    ├─ enable A20
    ├─ load raw kernel.bin sectors to 0x1000
    ├─ setup GDT
    ├─ switch to protected mode
    ├─ emit "BOOT_OK" on COM1
    └─ jump/call kernel entry at 0x1000

    entry.asm
    ├─ setup stack
    └─ call kernel_main

    kernel_main()
    ├─ serial_init()
    ├─ emit "KERNEL_INIT_OK" on COM1
    ├─ vga_init()
    ├─ idt_init()
    ├─ shell_init()              current shell phase
    ├─ emit "SHELL_READY"        current shell phase required
    └─ shell_run() or halt loop
```

## Common Issues

| Issue | Triệu chứng | Fix |
|---|---|---|
| Serial output rỗng | `build/serial.log` empty | Ensure markers use COM1, not VGA/BIOS |
| Chỉ có `BOOT_OK` | Kernel did not reach `kernel_main()` | Check raw kernel placement, GDT, stack, jump address |
| Chỉ có `KERNEL_INIT_OK` | Bootloader marker missing or disabled | Ensure bootloader initializes COM1 and emits `BOOT_OK` |
| Marker bị garbage | Baud/line format mismatch or early stack/register bug | Use 38400 8N1 setup and preserve registers in serial routines |
| Marker hiện 2 lần | Reboot loop or `kernel_main()` returned | Use `-no-reboot`; halt after panic/return |

## Tài liệu tham khảo

- [QEMU Documentation - Character devices and serial](https://www.qemu.org/docs/master/system/qemu-manpage.html)
- [OSDev Wiki - Serial Ports](https://wiki.osdev.org/Serial_Ports)
- [OSDev Wiki - Boot Sequence](https://wiki.osdev.org/Boot_Sequence)
