# AGENTS.md

## Project Overview
x86 bare metal teaching operating system written in C and x86 assembly (NASM syntax).
Runs on QEMU i386 emulator. Has QEMU-validated boot, shell `help`, syscall ABI, exception panic, paging, scheduler, memory-detection, ring-3 user-mode, and shell `echo ok` gates. These are evidence-scoped proofs, not a complete operating system claim.
Before advanced core work, read `harness-engineering/harness_profile.yaml` and `harness-engineering/13-agent-routing-and-risk/README.md`.

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
│   ├── isr.asm            # ISR stubs (keyboard IRQ1)
│   ├── kernel.c           # kernel_main()
│   ├── idt.c              # Interrupt Descriptor Table + PIC remap
│   ├── keyboard.c         # PS/2 keyboard driver
│   ├── shell.c            # Basic command shell
│   ├── string.c           # freestanding memcpy/memset/memmove/memcmp/strcmp
│   ├── vga.c              # VGA text mode driver
│   └── serial.c           # COM1 serial driver for automated testing
├── include/
│   ├── kernel.h
│   ├── idt.h
│   ├── timer.h
│   ├── memory.h
│   ├── keyboard.h
│   ├── shell.h
│   ├── serial.h
│   ├── string.h
│   └── vga.h
├── scripts/
│   ├── boot_test.sh       # Automated QEMU boot marker test with evidence logging
│   └── shell_test.sh      # QEMU monitor + VGA text validation for shell commands
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
make test       # boot marker test + shell runtime test
make clean
```

## Architecture Notes
- Build artifacts: `build/boot.bin`, `build/boot_config.inc`, `build/kernel.elf`, `build/kernel.bin`, `build/os.img`
- Boot sequence: BIOS -> `boot.bin` at 0x7C00 -> load raw `kernel.bin` at 0x1000 -> protected mode -> kernel entry
- Phase-1 loader uses BIOS-reported CHS geometry and currently requires `KERNEL_SECTORS <= 120`
- Kernel entry: `entry.asm` puts `_start` in `.entry`, sets stack, and calls `kernel_main()`
- Boot-marker tests read COM1 serial output via QEMU `-serial file:build/serial.log -monitor none`
- Shell-runtime tests use QEMU monitor `sendkey`, dump VGA text memory, and require visible `help` output.
- Required markers: `BOOT_OK`, `KERNEL_INIT_OK`, `SHELL_READY`
- Optional markers: `TESTS_PASS`
- Failure markers: `BOOT_DISK_ERROR`, `KERNEL_PANIC`
- Feature status is evidence-scoped: `make test` proves boot, COM1 markers, keyboard IRQ input, shell dispatch, VGA output, and `help` rendering.
- `make test-deep` adds syscall ABI, structured exceptions, paging map/unmap/write/unmap-fault evidence, explicit cooperative scheduler context execution, CMOS/QEMU memory-size detection, ring-3 user-mode transition with user/supervisor page fault, timer ticks, and `echo ok` shell I/O.
- Still not proven: timer-driven preemptive scheduling, per-process address-space isolation, memory allocator correctness, full memory map handling, filesystem, networking, or graphics.
- Current next work order: timer preemption proof, process address-space isolation proof, memory allocator proof, per-process paging proof, user-mode syscall negative-path proof.

## Memory Map
| Address | Content |
|---|---|
| 0x0000-0x03FF | Real mode IVT |
| 0x0400-0x04FF | BIOS Data Area |
| 0x7C00-0x7DFF | Boot sector |
| 0x1000+ | Kernel binary loaded from generated sector count |
| 0x90000 | Temporary stack |
| 0xB8000 | VGA text buffer |
| 0x100000-0x101FFF | Kernel page directory/table during paging tests |
| 0x100000+ | Extended memory, future heap after reserved kernel/test frames |

## Things to Avoid
- KHÔNG dùng `gcc` thường — dùng `i686-elf-gcc`
- KHÔNG dùng `-m32` — cross-compiler đã target i386
- KHÔNG dùng libc functions (`printf`, `malloc`) trong kernel
- KHÔNG assume compiler will avoid runtime helpers; provide freestanding `memcpy`, `memset`, `memmove`, `memcmp`
- KHÔNG assemble boot sector bằng `nasm -f elf32` để ghi vào disk image
- KHÔNG link `boot.bin` hoặc bootloader object vào kernel
- KHÔNG hardcode stale `KERNEL_SECTORS`; generate or validate it from `kernel.bin`
- KHÔNG let the phase-1 BIOS-geometry CHS loader exceed 120 kernel sectors
- KHÔNG dùng `-serial mon:stdio` as automated evidence channel
- KHÔNG chạy QEMU bằng root
- KHÔNG passthrough host disks/devices vào QEMU boot tests
- KHÔNG add filesystem/networking/graphics or extra shell breadth before the P0/P1 core-risk tasks in `harness_profile.yaml`
- KHÔNG claim timer preemption, process isolation, full memory protection, or allocator correctness without a targeted runtime test and updated claim status

## Available Skills
- `write-bootloader` — Viết/sửa flat boot sector 512 bytes
- `setup-gdt` — Cấu hình Global Descriptor Table cho protected mode
- `kernel-entry` — Kernel entry point, stack setup, call `kernel_main()`
- `serial-driver` — COM1 serial output for automated markers
- `compile-and-run` — Build + QEMU boot test loop
- `debug-kernel-panic` — Phân tích serial log, identify crash cause
