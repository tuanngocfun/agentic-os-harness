# AGENTS.md

## Project Overview
x86 bare metal teaching operating system written in C and x86 assembly (NASM syntax).
Runs on QEMU i386 emulator. Goal: boot successfully, initialize kernel, then grow toward a basic shell.

## Tech Stack
- Cross-compiler: i686-elf-gcc 13.2.0 (C, freestanding, no libc)
- Linker tools: i686-elf-gcc final link + i686-elf-objcopy
- Assembler: NASM 2.16.01 (Intel syntax)
- Build: GNU Make 4.3+
- Emulator: QEMU (qemu-system-i386)
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
│   ├── vga.c              # VGA text mode driver
│   └── serial.c           # COM1 serial driver for automated testing
├── include/
│   ├── kernel.h
│   ├── serial.h
│   ├── string.h
│   └── vga.h
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
- Phase-1 loader uses one CHS read and therefore requires `KERNEL_SECTORS <= 17`
- Kernel entry: `entry.asm` puts `_start` in `.entry`, sets stack, and calls `kernel_main()`
- Automated tests read COM1 serial output via QEMU `-serial file:build/serial.log -monitor none`
- Required markers: `BOOT_OK`, `KERNEL_INIT_OK`
- Optional markers: `SHELL_READY`, `TESTS_PASS`
- Failure markers: `BOOT_DISK_ERROR`, `KERNEL_PANIC`

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

## Things to Avoid
- KHÔNG dùng `gcc` thường — dùng `i686-elf-gcc`
- KHÔNG dùng `-m32` — cross-compiler đã target i386
- KHÔNG dùng libc functions (`printf`, `malloc`) trong kernel
- KHÔNG assume compiler will avoid runtime helpers; provide freestanding `memcpy`, `memset`, `memmove`, `memcmp`
- KHÔNG assemble boot sector bằng `nasm -f elf32` để ghi vào disk image
- KHÔNG link `boot.bin` hoặc bootloader object vào kernel
- KHÔNG hardcode stale `KERNEL_SECTORS`; generate or validate it from `kernel.bin`
- KHÔNG let phase-1 CHS single-read loader exceed 17 kernel sectors
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
