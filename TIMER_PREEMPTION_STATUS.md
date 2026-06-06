# Timer Preemption Status

## Current State (2026-06-06)

Timer preemption is implemented and has a targeted runtime gate.

Completed:
- IRQ0 and IRQ1 are enabled by the PIC master mask `0xFC`; bit value `0` means the IRQ is unmasked.
- `isr_stub_32` saves segment registers and general-purpose registers, calls `timer_interrupt(interrupted_esp)`, switches to the returned stack pointer, sends EOI, restores registers, and exits through `iretd`.
- `timer_interrupt()` increments the PIT tick counter and calls `scheduler_preempt()`.
- `process_create_preemptive()` builds an interrupt-frame-compatible initial kernel stack.
- `scheduler_preempt()` saves the interrupted task ESP and returns the selected task ESP.
- `make test-timer-preemption` proves two CPU-bound tasks that never call `yield()` both execute and emit `PREEMPT_OK`.

Evidence:
```bash
make test-timer-preemption
```

Required markers:
- `PREEMPT_TEST`
- `PREEMPT_A`
- `PREEMPT_B`
- `PREEMPT_OK`

Remaining limits:
- This file describes only the timer-preemption gate. Fairness, priority, and interrupt-critical-section behavior are separately covered by `make test-scheduler-safety`.
- Ring-3 syscall negative paths are separately covered by `make test-syscall-negative`.
- E820 memory-map handoff and physical frame allocation/free/reuse/exhaustion are separately covered by `make test-e820-frame`.
- SMP-grade scheduling and production-grade memory management are still not claimed.
