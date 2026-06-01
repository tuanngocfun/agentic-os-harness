# Memory Files — Persistent State Across Sessions

## Tại sao cần Memory Files?

Agents mất state sau mỗi session. Memory files encode state vào filesystem:

```
Session 1: Agent viết boot sector → boot test pass
Session 2: Agent quên → viết lại boot sector từ đầu → conflict
```

Với memory files:

```
Session 1: Agent viết boot sector → boot test pass → update progress.md
Session 2: Agent đọc progress.md → biết boot sector đã OK → chuyển sang GDT
```

## Cấu trúc Memory

```
agent_docs/
├── architecture.md       ← System design, key decisions
├── running_tests.md      ← How to run, what to expect
├── known_issues.md       ← Known bugs, workarounds
└── progress.md           ← Current task state
```

## Template progress.md

```markdown
# Agent Progress Log

## Current Task
Build x86 bare metal OS — kernel entry point setup

## Status: IN_PROGRESS

## Completed Steps
- [x] Boot sector written (boot.asm) — 2026-05-31 10:00
- [x] Boot test pass: BOOT_OK COM1 serial output — 2026-05-31 10:15
- [x] GDT setup (gdt.asm + gdt.c) — 2026-05-31 11:00
- [x] Boot test pass: KERNEL_INIT_OK — 2026-05-31 11:30
- [x] IDT setup (idt.asm + idt.c) — 2026-05-31 12:00

## Next Steps
- [ ] Kernel entry point (kernel.c: kernel_main)
- [ ] VGA text mode driver
- [ ] Serial output driver
- [ ] Basic shell

## Blockers
- None currently

## Decisions Made
- Decision: Bootloader marker must use COM1 serial, not BIOS/VGA text
  Reason: QEMU automated test captures COM1 through a dedicated `-serial file:build/serial.log`; `-serial mon:stdio` is human debug only because it multiplexes monitor and serial
  Alternatives considered: VGA-only marker — human-visible but not machine-verifiable in headless test

- Decision: Dùng monolithic kernel thay vì microkernel
  Reason: Đơn giản hơn cho teaching OS, ít IPC overhead
  Alternatives considered: Microkernel (seL4-style) — quá phức tạp cho giai đoạn này

- Decision: Dùng VGA text mode 0xB8000 cho output đầu tiên
  Reason: Đơn giản, không cần driver phức tạp
  Alternatives considered: Framebuffer graphics — cần driver phức tạp hơn
```

## Template architecture.md

```markdown
# OS Architecture

## Overview
x86 bare metal monolithic kernel, chạy trên QEMU i386.

## Memory Map
| Address Range | Usage |
|---|---|
| 0x0000 - 0x03FF | Real mode IVT |
| 0x0400 - 0x04FF | BIOS Data Area |
| 0x7C00 - 0x7DFF | Boot sector (loaded by BIOS) |
| 0x1000 - 0x7BFF | Kernel (loaded by boot sector) |
| 0xB8000 | VGA text mode buffer |
| 0x100000+ | Extended memory (heap) |

## Boot Sequence
1. BIOS loads boot sector at 0x7C00
2. Boot sector: enable A20, load kernel at 0x1000
3. Boot sector: set up GDT, switch to protected mode
4. Jump to kernel entry at 0x1000
5. kernel_main(): init IDT, VGA, serial, shell

## Module Dependencies
```
boot.asm
  └── gdt.asm (GDT setup)
       └── kernel.c:kernel_main()
            ├── idt.c (interrupt handling)
            ├── vga.c (text output)
            ├── serial.c (COM1 output)
            └── shell.c (command shell)
```

## Key Design Decisions
1. Monolithic kernel (not microkernel)
2. VGA text mode 0xB8000 (not framebuffer)
3. COM1 serial 0x3F8 (for automated testing)
4. i686-elf cross-compiler (not native gcc)
```

## Template known_issues.md

```markdown
# Known Issues

## Critical
- Triple fault when enabling paging before setting up page tables
  Workaround: Set up identity mapping first, then enable PG bit

## Warning
- QEMU hangs if boot sector doesn't set stack pointer before calls
  Workaround: Always `mov sp, 0x7C00` at start of boot sector

## Info
- GCC warns about missing prototype for kernel_main
  Fix: Add `void kernel_main(void)` declaration in header
```

## Khi nào update Memory Files?

| Event | File nào update |
|---|---|
| Hoàn thành 1 step | `progress.md` — mark [x], add timestamp |
| Quyết định architecture | `architecture.md` — add decision + reason |
| Phát hiện bug mới | `known_issues.md` — add issue + workaround |
| Thay đổi boot sequence | `architecture.md` — update boot sequence |
| Blocker xuất hiện | `progress.md` — add blocker description |

## Nguyên tắc

1. **Agent update memory files** sau mỗi significant action
2. **Agent đọc memory files** ở đầu mỗi session
3. **progress.md** là source of truth cho task state
4. **KHÔNG** delete completed steps — giữ history

## Tài liệu tham khảo

- [What Is Harness Engineering AI? - Atlan](https://atlan.com/know/what-is-harness-engineering/)
- [The importance of Agent Harness in 2026 - Philschmid](https://www.philschmid.de/agent-harness-2026)
