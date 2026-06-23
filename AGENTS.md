# AGENTS.md

## Project Overview
x86 bare metal teaching operating system written in C and x86 assembly (NASM syntax).
Runs on QEMU i386 emulator. Has QEMU-validated stage-2 boot, shell `help`, syscall ABI, ring-3 syscall negative paths, exception panic, paging, cooperative scheduler, timer preemption, scheduler priority/fairness safety proof, E820 memory-map detection, physical frame allocator lifecycle, heap allocator, ring-3 user-mode, per-process address-space switching, VFS-backed file syscalls, ELF loading, true fork parent/child return, blocking wait with zombie reap, exit scheduling, exec image replacement, per-process descriptor ownership, argv/envp exec startup, COW/demand/guard VM evidence, waitpid options, minimal signal delivery, blocking pipes, and shell `echo ok` gates. These are evidence-scoped proofs, not a complete operating system claim. Current security posture is red/blue regression: `make test-red-team` attempts known attacks, proves current blue controls block them, and writes `build/red-team/findings.jsonl`.
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
- Build artifacts: `build/boot.bin`, `build/stage2.bin`, `build/boot_config.inc`, `build/kernel.elf`, `build/kernel.bin`, `build/os.img`
- Boot sequence: BIOS -> `boot.bin` at 0x7C00 -> load `stage2.bin` at 0x90000 -> stage 2 loads raw `kernel.bin` at 0x1000 by LBA -> protected mode -> kernel entry
- Stage 2 reserves 32 disk sectors and places the kernel at LBA 33, removing the previous phase-1 120-sector CHS kernel cap
- Kernel entry: `entry.asm` puts `_start` in `.entry`, sets stack, and calls `kernel_main()`
- Boot-marker tests read COM1 serial output via QEMU `-serial file:build/serial.log -monitor none`
- Shell-runtime tests use QEMU monitor `sendkey`, dump VGA text memory, and require visible `help` output.
- Required markers: `STAGE2_OK`, `BOOT_OK`, `KERNEL_INIT_OK`, `SHELL_READY`
- Optional markers: `TESTS_PASS`
- Failure markers: `BOOT_DISK_ERROR`, `KERNEL_PANIC`
- Feature status is evidence-scoped: `make test` proves boot, COM1 markers, keyboard IRQ input, shell dispatch, VGA output, and `help` rendering.
- `make test-deep` adds syscall ABI, ring-3 syscall negative-path validation, VFS-backed ring-3 file syscall evidence, ELF loader-prep evidence, process syscall + VFS-backed ELF entry transfer, fork parent/child return, blocking wait/exit/zombie reap, copied-address-space isolation, exec image replacement, argv/envp startup, per-process descriptor ownership, waitpid WNOHANG/specific-child/status/negative-path evidence, minimal SIGTERM/SIGKILL/SIGCHLD-pending signal behavior, blocking pipe create/read/write/EOF/broken-write evidence, structured exceptions, paging map/unmap/write/unmap-fault evidence, explicit cooperative scheduler context execution, timer-driven preemption evidence, E820-backed usable-memory detection, physical frame allocation/free/reuse/exhaustion evidence, heap allocator behavior, ring-3 user-mode transition with user/supervisor page fault, per-process CR3/address-space isolation evidence, timer ticks, scheduler priority/fairness safety evidence, ramdisk block-device evidence, kernel VFS + flat SimpleFS evidence, COW/demand/guard VM evidence, and `echo ok` shell I/O.
- `make test-red-team` and `make test-blue-team` are separate from `make test-deep`; they are guest-only adversarial regression gates, not production security passes.
- Still not proven: swap, automatic stack growth, full POSIX signal handlers/masks, nonblocking pipe mode, select/poll, production-grade virtual memory, SMP-safe scheduling, persistent storage, networking, or graphics.
- Current next work order: continue guest-only red/blue fuzzing and lifecycle stress gates without broadening into networking, graphics, or untested shell breadth.

## Memory Map
| Address | Content |
|---|---|
| 0x0000-0x03FF | Real mode IVT |
| 0x0400-0x04FF | BIOS Data Area |
| 0x7C00-0x7DFF | Boot sector |
| 0x1000+ | Kernel binary loaded from generated sector count |
| 0x70000 | Temporary stage-2 real/protected-mode stack |
| 0x90000 | Stage-2 loader body |
| 0xB8000 | VGA text buffer |
| 0x100000-0x101FFF | Boot kernel page directory/table |
| 0x200000-0x2FFFFF | Fixed 1 MiB heap used by `kmalloc`/`kfree` |
| 0x300000+ | Page-frame allocator pool for page directories/tables/test frames |
| 0x80000+ | E820 handoff buffer populated by the real-mode bootloader |

## Things to Avoid
- KHÔNG dùng `gcc` thường — dùng `i686-elf-gcc`
- KHÔNG dùng `-m32` — cross-compiler đã target i386
- KHÔNG dùng libc functions (`printf`, `malloc`) trong kernel
- KHÔNG assume compiler will avoid runtime helpers; provide freestanding `memcpy`, `memset`, `memmove`, `memcmp`
- KHÔNG assemble boot sector bằng `nasm -f elf32` để ghi vào disk image
- KHÔNG link `boot.bin` hoặc bootloader object vào kernel
- KHÔNG hardcode stale `KERNEL_SECTORS`; generate or validate it from `kernel.bin`
- KHÔNG reintroduce a phase-1-only kernel loader or stale 120-sector CHS kernel cap
- KHÔNG dùng `-serial mon:stdio` as automated evidence channel
- KHÔNG chạy QEMU bằng root
- KHÔNG passthrough host disks/devices vào QEMU boot tests
- KHÔNG claim full POSIX signals, nonblocking pipes, select/poll, networking, graphics, persistent storage, or extra shell breadth without targeted runtime tests and updated `harness_profile.yaml` claim status
- KHÔNG claim full memory protection, full userland syscall coverage, production-grade frame/heap management, persistent storage, networking, or graphics without targeted runtime tests and updated claim status

## Available Skills
- `write-bootloader` — Viết/sửa flat boot sector 512 bytes
- `setup-gdt` — Cấu hình Global Descriptor Table cho protected mode
- `kernel-entry` — Kernel entry point, stack setup, call `kernel_main()`
- `serial-driver` — COM1 serial output for automated markers
- `compile-and-run` — Build + QEMU boot test loop
- `debug-kernel-panic` — Phân tích serial log, identify crash cause
## Static And Meta-Loop Gates

- Open `agentic-os.code-workspace` (or the `os` repository folder) so VS Code uses the freestanding i686 cross toolchain instead of host headers.
- Run `make test-static-analysis` for C/header/ASM-ABI/editor changes.
- Run `make test-red-team-tooling` for tooling or test-oracle findings.
- Run `make test-meta-loop` before claiming a red/blue patch cycle is complete.
