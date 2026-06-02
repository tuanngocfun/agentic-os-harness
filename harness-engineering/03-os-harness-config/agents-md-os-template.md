# AGENTS.md — Template cho OS Project (x86 Bare Metal + C)

## Copy-paste vào root project

```markdown
# AGENTS.md

## Project Overview
x86 bare metal teaching operating system written in C and x86 assembly (NASM syntax).
Runs on QEMU i386 emulator. Goal: boot successfully, initialize kernel, then grow toward a basic shell.

## Tech Stack
- Cross-compiler: i686-elf-gcc 13.2+ (C, freestanding, no libc)
- Linker tools: i686-elf-gcc final link + i686-elf-objcopy
- Assembler: NASM 2.16+ (Intel syntax)
- Build: GNU Make 4.3+
- Emulator: QEMU 8.2+ (qemu-system-i386)
- Host: Ubuntu Server, run as normal user

## Project Structure
```
.
├── boot/
│   └── boot.asm          # Flat boot sector, 512 bytes, 0xAA55 signature
├── kernel/
│   ├── entry.asm          # ELF32 kernel entry point
│   ├── kernel.c           # kernel_main()
│   ├── string.c           # freestanding memcpy/memset/memmove/memcmp
│   ├── idt.c              # Interrupt Descriptor Table
│   ├── vga.c              # VGA text mode driver
│   └── serial.c           # COM1 serial driver for automated testing
├── include/
│   ├── kernel.h
│   ├── vga.h
│   └── serial.h
├── scripts/
│   └── boot_test.sh       # Automated QEMU boot test
├── linker.ld              # Kernel linked at 0x1000
├── Makefile
└── AGENTS.md
```

## Setup Commands
```bash
# Install dependencies (Ubuntu)
sudo apt-get install nasm qemu-system-x86 make

# Build cross-compiler if i686-elf-gcc is not installed
# See: https://wiki.osdev.org/GCC_Cross-Compiler

make all
make run
make run-serial
make test
make clean
```

## Architecture Notes
- Build artifacts: `build/boot.bin`, `build/boot_config.inc`, `build/kernel.elf`, `build/kernel.bin`, `build/os.img`
- Boot sequence: BIOS -> `boot.bin` at 0x7C00 -> load raw `kernel.bin` at 0x1000 -> protected mode -> kernel entry
- Phase-1 loader uses track-rolling CHS and currently requires `KERNEL_SECTORS <= 120`; larger kernels require LBA or 2-stage boot.
- Kernel entry: `entry.asm` puts `_start` in `.entry`, sets stack, and calls `kernel_main()`
- Automated tests read COM1 serial output via QEMU `-serial file:build/serial.log -monitor none`; `-serial mon:stdio` is human debug only
- Core required markers: `BOOT_OK`, `KERNEL_INIT_OK`
- Current shell phase required marker: `SHELL_READY`
- Optional markers: `TESTS_PASS`
- Failure markers: `BOOT_DISK_ERROR`, `KERNEL_PANIC`
- Xem chi tiết: `agent_docs/architecture.md`

## Memory Map
| Address | Content |
|---|---|
| 0x0000-0x03FF | Real mode IVT |
| 0x0400-0x04FF | BIOS Data Area |
| 0x7C00-0x7DFF | Boot sector |
| 0x1000-0x7BFF | Kernel binary |
| 0x90000 | Temporary stack |
| 0xB8000 | VGA text buffer |
| 0x100000+ | Extended memory, future heap |

## Build & Test Flow
```
1. nasm -f elf32 kernel/entry.asm -o build/entry.o
2. i686-elf-gcc -ffreestanding -fno-pic -fno-pie -Iinclude -MMD -MP -c kernel/kernel.c -o build/kernel.o
3. i686-elf-gcc -T linker.ld -ffreestanding -nostdlib build/entry.o build/kernel.o ... -lgcc -o build/kernel.elf
4. i686-elf-objcopy -O binary build/kernel.elf build/kernel.bin
5. generate build/boot_config.inc from kernel.bin size with `wc -c`; fail if CHS phase-1 sector count exceeds 120
6. nasm -Ibuild/ -f bin boot/boot.asm -o build/boot.bin
7. verify build/boot.bin is exactly 512 bytes
8. dd if=build/boot.bin of=build/os.img bs=512 count=1 conv=notrunc
9. dd if=build/kernel.bin of=build/os.img bs=512 seek=1 conv=notrunc
10. qemu-system-i386 -drive file=build/os.img,format=raw -serial file:build/serial.log -monitor none -nic none -display none -no-reboot
11. parse exact whole-line required markers in build/serial.log -> PASS
```

## Testing Instructions
- Automated: `make test` -> exit code 0 = boot success
- Core required serial markers: `BOOT_OK`, `KERNEL_INIT_OK`
- Current shell phase required serial marker: `SHELL_READY`
- Optional serial markers: `TESTS_PASS`
- Current shell phase also runs a runtime shell test that proves `help` through VGA output.
- Timeout: 30 seconds max
- Xem chi tiết: `agent_docs/running_tests.md`

## Things to Avoid
- KHÔNG dùng `gcc` thường — dùng `i686-elf-gcc`
- KHÔNG dùng `-m32` — cross-compiler đã target i386
- KHÔNG dùng libc functions (`printf`, `malloc`) trong kernel
- KHÔNG assume compiler will avoid runtime helpers; provide freestanding `memcpy`, `memset`, `memmove`, `memcmp`
- KHÔNG dùng `-nostdinc` nếu vẫn include `<stdint.h>` từ compiler headers
- KHÔNG assemble boot sector bằng `nasm -f elf32` để ghi vào disk image
- KHÔNG link `boot.bin` hoặc bootloader object vào kernel
- KHÔNG hardcode stale `KERNEL_SECTORS`; generate or validate it from `kernel.bin`
- KHÔNG let phase-1 track-rolling CHS loader exceed 120 kernel sectors
- KHÔNG override `BUILD_DIR` or `OS_IMG` to unsafe paths; Makefile guards must reject this before `dd` or `clean`
- KHÔNG vượt 512-byte first-stage boot sector; split into 2-stage loader nếu cần
- KHÔNG expect BIOS/VGA text xuất hiện trong `build/serial.log`
- KHÔNG dùng `grep -q` substring checks cho markers; use exact whole-line parsing
- KHÔNG dùng `-serial mon:stdio` as automated evidence channel
- KHÔNG chạy QEMU bằng root
- KHÔNG passthrough host disks/devices vào QEMU boot tests

## Available Skills
- `write-bootloader` — Viết/sửa flat boot sector 512 bytes
- `setup-gdt` — Cấu hình Global Descriptor Table cho protected mode
- `kernel-entry` — Kernel entry point, stack setup, call `kernel_main()`
- `serial-driver` — COM1 serial output for automated markers
- `compile-and-run` — Build + QEMU boot test loop
- `debug-kernel-panic` — Phân tích serial log, identify crash cause

## Sub-Agents
- `orchestrator` — Phân công tasks, merge results
- `code-reviewer` — Read-only review
- `test-runner` — Chạy `make test`, parse serial output
- `debugger` — Phân tích boot failure logs
```

## Tùy chỉnh cho project cụ thể

1. Update sector count nếu kernel lớn hơn bootloader đang đọc.
2. Update memory map nếu kernel load address khác `0x1000`.
3. Chỉ thêm `SHELL_READY` vào required test khi shell thật sự tồn tại.
4. Nếu chuyển sang 2-stage bootloader hoặc Multiboot, update toàn bộ artifact contract.
