# P0 Critical Fixes - OS Preemptive Scheduling

**Status:** Ready to implement  
**Priority:** P0 (blocks all advanced work)  
**Estimated Time:** 2-4 hours

---

## Issue #1: Timer IRQ is Masked - No Preemption Possible

### Root Cause
`kernel/idt.c:53` sets PIC master mask to `0xFC`:
```c
outb(0x21, 0xFC);  // Binary: 11111100
```

**Binary breakdown:**
- Bit 0 (IRQ0 - Timer): **1 = MASKED** ❌
- Bit 1 (IRQ1 - Keyboard): **0 = ENABLED** ✓
- Bits 2-7: **MASKED**

This means **IRQ0 (timer) never fires interrupts**. The timer handler is registered in the IDT but never gets called.

### Fix Required
Change mask to `0xFE` (binary: 11111110):
```c
outb(0x21, 0xFE);  // Enable IRQ0 (timer) + IRQ1 (keyboard)
```

### Impact
- Timer will start firing interrupts at 100Hz (configured in kernel/timer.c:18)
- Currently timer_handler() only increments counter
- Need to wire timer_handler() → scheduler_tick() → scheduler_schedule()

---

## Issue #2: Timer Handler Not Wired to Scheduler

### Current State
`kernel/timer.c:14-16`:
```c
void timer_handler(void) {
    timer_ticks++;  // Only increments counter, no scheduling
}
```

`kernel/scheduler.c:63-66`:
```c
void scheduler_tick(void) {
    if (!scheduler_initialized) return;
    scheduler_schedule();  // This is never called!
}
```

### Fix Required
1. Call `scheduler_tick()` from `timer_handler()`
2. Ensure scheduler doesn't break timer tick counting

```c
// kernel/timer.c
#include "scheduler.h"

void timer_handler(void) {
    timer_ticks++;
    scheduler_tick();  // Trigger preemptive scheduling
}
```

---

## Issue #3: Context Switch Doesn't Save Full CPU State for Preemption

### Current State
`kernel/scheduler.c:89-91` only switches when processes voluntarily yield:
```c
if (prev && prev != current && prev->state == PROCESS_READY) {
    context_switch(&prev->esp, &current->esp);  // Only called during yield()
}
```

### Problem
Preemptive context switch happens **during timer interrupt**, which means:
- IRQ pushes: SS, ESP, EFLAGS, CS, EIP onto stack
- ISR pushes: error code (if applicable)
- We need to save **all general-purpose registers**

### Fix Required
Modify `kernel/isr.asm` to push all registers before calling timer_handler:
```nasm
isr_stub_32:  ; Timer IRQ
    pusha     ; Push all general-purpose registers (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10  ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    call timer_handler
    
    pop gs
    pop fs
    pop es
    pop ds
    popa
    
    iret
```

**Alternative:** Modify `context_switch()` to be preemption-aware and save/restore from interrupt stack frame.

---

## Issue #4: No Test for Preemptive Scheduling

### Current State
`make test-scheduler` only tests cooperative scheduling (manual yield())

### Fix Required
Create `scripts/timer_preemption_test.sh` that:
1. Builds kernel with `ENABLE_TIMER_PREEMPTION_SELFTEST`
2. Creates two processes:
   - Process A: infinite loop, never calls yield()
   - Process B: sets a marker "PREEMPT_B_EXECUTED"
3. Proof: If marker appears, preemption works (B got CPU despite A hogging)

---

## Implementation Plan

### Step 1: Enable Timer IRQ (5 minutes)
- [ ] Change `kernel/idt.c:53` from `0xFC` to `0xFE`
- [ ] Rebuild and verify timer interrupts fire
- [ ] Check `timer_get_ticks()` increments automatically (not just during yield)

### Step 2: Wire Scheduler to Timer (10 minutes)
- [ ] Add `#include "scheduler.h"` to `kernel/timer.c`
- [ ] Call `scheduler_tick()` from `timer_handler()`
- [ ] Test that scheduler runs during timer IRQ

### Step 3: Fix Context Switch for Preemption (30 minutes)
- [ ] Save all registers in timer ISR (modify `kernel/isr.asm`)
- [ ] Test that context switch preserves registers correctly
- [ ] Add marker test to verify preemption actually works

### Step 4: Create Preemption Test (20 minutes)
- [ ] Create `scripts/timer_preemption_test.sh`
- [ ] Add selftest code in `kernel/kernel.c` under `ENABLE_TIMER_PREEMPTION_SELFTEST`
- [ ] Add to `make test-deep` target
- [ ] Update `harness_profile.yaml` claim status

### Step 5: Update Documentation (10 minutes)
- [ ] Update `AGENTS.md` to remove "timer preemption" from pending work
- [ ] Update `harness_profile.yaml` scheduler claim from "explicit context switch" to "preemptive scheduling"
- [ ] Add evidence to `build/evidence.jsonl`

---

## Risk Assessment

### High Risk
- **Timer interrupts firing too fast**: If scheduler is slow, could spend all time context switching
  - Mitigation: Keep scheduler_schedule() fast, consider time quantum counter
  
### Medium Risk  
- **Re-entrancy**: Timer could fire during critical section (e.g., inside scheduler)
  - Mitigation: Disable interrupts during scheduler operations (cli/sti)

### Low Risk
- **Breaking existing tests**: Timer was already configured, just masked
  - Mitigation: All existing tests pass with timer enabled (they don't rely on it being off)

---

## Success Criteria

After implementing these fixes:
1. ✓ `make test-deep` includes `test-timer-preemption` and it PASSES
2. ✓ Process A never calls yield() but Process B still executes → proof of preemption
3. ✓ `harness_profile.yaml` updated with `scheduler: "claimable_with_preemptive_scheduling_test"`
4. ✓ Evidence log contains passing preemption test
5. ✓ Timer ticks increment automatically (not just during yield)

---

## Follow-Up Work (Not P0)

After preemption works:
- P1: Per-process address-space isolation (CR3 switching)
- P1: Real memory allocator (kmalloc/kfree)
- P2: User-mode syscall negative paths
- P2: More robust scheduler (time quantum, priority, fairness)

---

**Ready to implement?** Start with Step 1 (5-minute fix).
