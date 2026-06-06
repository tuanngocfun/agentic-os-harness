# P0 Critical Fixes - Completed Status

**Status:** P0 timer preemption completed on 2026-06-05.
**Validated by:** `make test-timer-preemption`.

## Corrected PIC Mask Note

The previous diagnosis said `0xFC` masks IRQ0. That was wrong.

PIC interrupt-mask bits use `1 = masked` and `0 = enabled`.

`0xFC` is `11111100`, so:
- bit 0 = `0`: IRQ0 timer enabled
- bit 1 = `0`: IRQ1 keyboard enabled
- bits 2-7 = `1`: masked

The source now documents this in `kernel/idt.c`.

## Implemented

- Timer IRQ path uses `timer_interrupt(interrupted_esp)` from `kernel/isr.asm`.
- IRQ32 saves/restores segment and general-purpose registers and returns through `iretd`.
- `scheduler_preempt()` saves the interrupted ESP and switches to the selected runnable task.
- `process_create_preemptive()` creates an interrupt-frame-compatible initial stack.
- `scripts/timer_preemption_test.sh` requires `PREEMPT_A`, `PREEMPT_B`, and `PREEMPT_OK`.
- `make test-deep` includes `test-timer-preemption`.
- `harness_profile.yaml` now claims scheduler preemption only through the targeted gate.

## Also Completed From The Follow-Up List

- Per-process page directories with CR3 switching: validated by `make test-address-space`.
- Same virtual address mapped to distinct physical frames across processes: validated by `ADDRSPACE_ISOLATION_OK`.
- Fixed 1 MiB heap allocator with `kmalloc`/`kfree`: validated by `make test-allocator`.

## Remaining Work

- Ring-3 syscall negative-path proof.
- E820-style memory-map handling and physical frame free/reuse accounting.
- Scheduler safety/fairness tests beyond the narrow two-task preemption proof.
