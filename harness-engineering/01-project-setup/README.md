# 01 — Project Setup

## Mục tiêu

Section này là preflight trước khi agent viết OS code. Nó khóa environment, artifact contract, và safety assumptions để tránh agent bắt đầu từ một setup mơ hồ.

## Host Assumptions

- Host: Ubuntu Server hoặc Linux dev machine.
- User: normal unprivileged user.
- Target: x86 i386 bare metal teaching OS.
- Emulator: QEMU with image file, not host disk passthrough.

## Required Tools

| Tool | Why |
|---|---|
| `i686-elf-gcc` | Freestanding cross-compiler for kernel C |
| `i686-elf-ld` | Low-level linker available to the compiler driver |
| `i686-elf-objcopy` | Convert `kernel.elf` to raw `kernel.bin` |
| `nasm` | Assemble boot sector and kernel entry |
| `qemu-system-i386` | Run automated boot tests |
| `make` | Build orchestration |
| `git` | Source-of-truth, diff, handoff, and tracked artifact checks |
| `wc`, `dd`, `grep`, `timeout`, `sha256sum` | Validation scripts |

## Preflight Checks

```bash
i686-elf-gcc --version
i686-elf-objcopy --version
nasm -v
qemu-system-i386 --version
make --version
git --version
```

Git preflight, when the harness lives inside a repo:

```bash
.agent/skills/git-change-management/scripts/git_preflight.sh
```

## Toolchain Bootstrap Gate

If the cross-compiler is not already installed:
- Human approval required before downloading/building GCC or binutils.
- Pin exact versions in a manifest.
- Verify GNU signatures or SHA256 checksums before extraction.
- Record tool versions in the first evidence entry.

## Initial Directory Layout

```
.
├── boot/
│   └── boot.asm
├── kernel/
│   ├── entry.asm
│   ├── kernel.c
│   ├── string.c
│   ├── serial.c
│   └── vga.c
├── include/
│   ├── string.h
│   └── serial.h
├── scripts/
│   └── boot_test.sh
├── linker.ld
├── Makefile
└── AGENTS.md
```

## Setup Gates

- Toolchain gate: all required commands exist.
- Safety gate: QEMU command uses `build/os.img`, not `/dev/sdX`.
- Build gate: Makefile creates `boot.bin`, `boot_config.inc`, `kernel.elf`, `kernel.bin`, `os.img`.
- Boot gate: `make test` captures dedicated COM1 markers in `build/serial.log`, with QEMU diagnostics in `build/qemu.log`.
- Discovery gate: root `llms.txt` exists and links to AGENTS, validation, safety, and skills.
- Git gate: repo root is known, worktree status is reported, generated artifacts are ignored/untracked, and no Git write action is performed without explicit approval.

## Notes

- Do not start with production OS scope.
- Do not use host `gcc` for kernel code.
- Do not make shell readiness a required marker until shell exists.
- Do not continue after preflight failure; fix environment first.
- Do not commit or stage generated build artifacts.
