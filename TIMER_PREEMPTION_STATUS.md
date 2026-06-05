# Timer Preemption Status

## Current State (2026-06-05)

### Infrastructure: READY ✓
- [x] Timer handler (`timer_handler` in `kernel/timer.c`)
- [x] Timer handler wired to scheduler (`scheduler_tick()`)
- [x] Timer test validates IRQ0 can fire (29-31 ticks/5s)
- [x] PIT initialized at ~18.2 Hz

### IRQ0 Status: MASKED (Intentional)
- **PIC Mask:** `0xFC` (IRQ0 masked, IRQ1 keyboard enabled)
- **Location:** `kernel/idt.c:53`
- **Reason:** Enabling IRQ0 breaks shell keyboard input processing

### Why Timer Is Masked

When IRQ0 is unmasked (`0xFE`), timer interrupts fire at 18.2 Hz and call `timer_handler()` → `scheduler_tick()`. However:

1. **Shell test fails**: QEMU keyboard event simulation doesn't work with timer interrupts active
2. **Root cause**: We don't have interrupt-driven context switching yet
3. **Current `scheduler_tick()`**: Intentionally disabled (comment at line 63-70 in `kernel/scheduler.c`)

### Test Evidence

**With IRQ0 enabled (`0xFE`):**
```bash
$ bash scripts/timer_preemption_test.sh
[PASS] Timer IRQ0 enabled: 29 ticks in 5s
[PASS] Timer handler is firing
```

**Default build (IRQ0 masked `0xFC`):**
```bash
$ make test
[PASS] BOOT_OK
[PASS] KERNEL_INIT_OK  
[PASS] SHELL_READY
[PASS] help command rendered
=== ALL TESTS PASSED ===
```

## What's Missing for Full Preemption

### P0: Interrupt-Driven Context Switching

Current `context_switch()` (in `kernel/usermode.asm`) only works for **cooperative** `yield()`:
- Saves/restores registers manually
- Expects both old and new task ESPs as arguments
- Cannot handle interrupt frames

**Required for preemptive switching:**

1. **Interrupt frame handling**
   - Save ALL registers (EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP, EIP, EFLAGS)
   - Handle CPU-pushed interrupt frame (CS, EIP, EFLAGS, SS, ESP)
   - Preserve interrupted context completely

2. **Scheduler integration**
   - `scheduler_tick()` must pick next task
   - Switch to next task's saved ESP
   - Return via `iretd` to resume next task

3. **Stack frame setup**
   - Each process needs interrupt-compatible stack frame
   - Initial ESP must point to fake interrupt frame
   - Kernel stack separate from user stack

### Implementation Checklist

- [ ] Implement `interrupt_context_switch()` in ASM
- [ ] Modify `process_create()` to set up interrupt-compatible stack
- [ ] Enable `scheduler_schedule()` call in `scheduler_tick()`
- [ ] Change PIC mask to `0xFE` (unmask IRQ0)
- [ ] Test preemptive multitasking with 2+ CPU-bound tasks
- [ ] Verify shell still works with timer interrupts active

## Testing Strategy

### Phase 1: Infrastructure (DONE ✓)
```bash
# Proves timer IRQ0 can be enabled
make test-timer-preemption
```

### Phase 2: Preemptive Switching (TODO)
```c
// Create two CPU-bound tasks
void task_a() { while(1) serial_puts("A\n"); }
void task_b() { while(1) serial_puts("B\n"); }

// Should see interleaved output: A B A B A B...
```

### Phase 3: Shell Integration (TODO)
```bash
# Verify shell works with timer interrupts
bash scripts/shell_test.sh  # Must pass with PIC mask = 0xFE
```

## Related Files

- `kernel/idt.c:53` - PIC mask configuration
- `kernel/timer.c:16` - timer_handler() calls scheduler_tick()
- `kernel/scheduler.c:63` - scheduler_tick() disabled pending preemption
- `kernel/usermode.asm:1` - context_switch() (cooperative only)
- `scripts/timer_preemption_test.sh` - Timer infrastructure test

## References

- P0_CRITICAL_FIXES.md - Implementation plan for timer preemption
- LINUS_AUDIT.md - Original diagnosis of missing preemption
- GPT5_ASSESSMENT.md - Overall OS status
