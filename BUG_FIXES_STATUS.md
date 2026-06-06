# Critical Bug Fixes - Implementation Status

**Date:** 2026-06-06  
**Goal:** Fix 6 critical bugs identified by Gemini 3.5 Flash, GPT-5.5, and Claude 4.6 Sonnet

---

## Bug #1: Stack Memory Leak in process.c

### Problem
- `allocate_stack()` only increments `stack_offset`, never decrements
- `process_destroy()` marks process as DEAD but doesn't reclaim stack
- After 16 process creates, stack_pool (64KB) is exhausted forever
- System can no longer create processes even if only 1 is running

### Current Code
```c
static uint32_t stack_offset = 0;

static uint32_t *allocate_stack(uint32_t size) {
    if (stack_offset + (size / sizeof(uint32_t)) > (sizeof(stack_pool) / sizeof(stack_pool[0]))) {
        return NULL;
    }
    uint32_t *stack = &stack_pool[stack_offset];
    stack_offset += size / sizeof(uint32_t);  // Only moves forward
    return stack;
}

void process_destroy(struct process *proc) {
    // ... marks PROCESS_DEAD but never reclaims stack
}
```

### Solution
Add a free list to track available stack slots:

```c
// Stack management with free list
#define STACK_SLOTS 16
#define STACK_SLOT_SIZE (KERNEL_STACK_SIZE / sizeof(uint32_t))

static uint32_t stack_pool[STACK_SLOTS * STACK_SLOT_SIZE];
static int stack_free_bitmap = 0xFFFF;  // All slots free initially

static uint32_t *allocate_stack(uint32_t size) {
    if (size != KERNEL_STACK_SIZE) return NULL;
    
    // Find first free slot
    for (int i = 0; i < STACK_SLOTS; i++) {
        if (stack_free_bitmap & (1 << i)) {
            stack_free_bitmap &= ~(1 << i);  // Mark as used
            return &stack_pool[i * STACK_SLOT_SIZE];
        }
    }
    return NULL;  // No free slots
}

static void free_stack(uint32_t *stack) {
    if (!stack) return;
    
    // Find slot index
    int slot = (stack - stack_pool) / STACK_SLOT_SIZE;
    if (slot >= 0 && slot < STACK_SLOTS) {
        stack_free_bitmap |= (1 << slot);  // Mark as free
    }
}

void process_destroy(struct process *proc) {
    if (!proc || proc->state == PROCESS_DEAD) return;
    
    // Free the stack
    free_stack(proc->kernel_stack);
    
    // Clear process state
    proc->state = PROCESS_DEAD;
    proc->pid = 0;
    proc->kernel_stack = NULL;
    process_count--;
}
```

### Status
- **DONE**: Bitmap-backed stack slot reuse is implemented in `kernel/process.c`
- **Test**: Covered by process/scheduler regression gates; broader process-lifecycle stress remains future work

---

## Bug #2: Incomplete Pointer Validation in syscall.c

### Problem
- `is_user_pointer_valid()` only checks first and last page
- If buffer spans 3+ pages and middle page is unmapped → kernel page fault → panic
- Example: 12KB buffer spanning 3 pages, middle page unmapped

### Current Code
```c
static int is_user_pointer_valid(uint32_t ptr, uint32_t size) {
    if (ptr < USER_SPACE_START) return 0;
    if (ptr >= USER_SPACE_END) return 0;
    if (size > 0 && (ptr + size) > USER_SPACE_END) return 0;
    if (size > 0 && (ptr + size) < ptr) return 0;
    
    // Only checks first page
    if (!paging_is_mapped(ptr)) return 0;
    if (!paging_is_user_accessible(ptr)) return 0;
    
    // Only checks last page
    if (size > 0) {
        uint32_t last = ptr + size - 1;
        if (!paging_is_mapped(last)) return 0;
        if (!paging_is_user_accessible(last)) return 0;
    }
    
    return 1;
}
```

### Solution
Check every page in the range:

```c
static int is_user_pointer_valid(uint32_t ptr, uint32_t size) {
    if (ptr < USER_SPACE_START) return 0;
    if (ptr >= USER_SPACE_END) return 0;
    if (size > 0 && (ptr + size) > USER_SPACE_END) return 0;
    if (size > 0 && (ptr + size) < ptr) return 0;  // Overflow check
    
    // Check every page in the range
    uint32_t start_page = ptr & ~0xFFF;
    uint32_t end_page = (ptr + size - 1) & ~0xFFF;
    
    for (uint32_t page = start_page; page <= end_page; page += 0x1000) {
        if (!paging_is_mapped(page)) return 0;
        if (!paging_is_user_accessible(page)) return 0;
    }
    
    return 1;
}
```

### Status
- **DONE**: Syscall pointer validation checks every page in the user buffer range
- **Test**: Covered by `make test-syscall-negative`

---

## Bug #3: Missing Default Exception Handlers

### Problem
- Only vectors 0, 6, 13, 14 have handlers
- Other exceptions (8=Double Fault, 12=Stack Fault, etc.) have IDT entry with flags=0
- Causes triple fault and instant reset with no panic log

### Solution
Add default handler for all vectors 0-31:

```asm
; In isr.asm
%macro ISR_NOERRCODE_DEFAULT 1
global isr_stub_%1
isr_stub_%1:
    push dword 0
    push dword %1
    jmp isr_common_stub_default
%endmacro

; Generate stubs for all unused vectors
ISR_NOERRCODE_DEFAULT 1
ISR_NOERRCODE_DEFAULT 2
ISR_NOERRCODE_DEFAULT 3
; ... etc for all vectors 0-31

isr_common_stub_default:
    pusha
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp
    call unhandled_exception_handler
    add esp, 4
    
    jmp $  ; Hang
```

```c
// In idt.c
void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    
    // Register default handlers for ALL vectors
    extern void isr_stub_0(void), isr_stub_1(void), isr_stub_2(void); // ...
    
    for (int i = 0; i < 32; i++) {
        // Will be overridden by specific handlers below
        idt_set_gate(i, (uint32_t)default_isr_table[i], 0x08, 0x8E);
    }
    
    // Then register specific handlers
    idt_set_gate(0, (uint32_t)isr_stub_0, 0x08, 0x8E);  // Div by zero
    // ... etc
}

void unhandled_exception_handler(uint32_t vector) {
    serial_puts("UNHANDLED_EXCEPTION:");
    serial_put_uint32(vector);
    serial_puts("\n");
}
```

### Status
- **TODO**: Implement default handlers
- **Test**: Trigger vector 8 (double fault) to verify panic

---

## Bug #4: Cooperative/Preemptive yield() Conflict

### Problem
- `context_switch()` expects cooperative frame (ebp/ebx/esi/edi + ret addr)
- Preemptive tasks have interrupt frame (segment regs + pusha + iretd)
- If preemptive task calls `yield()` → stack mismatch → crash

### Solution Option A: Forbid yield() from preemptive tasks
```c
void yield(void) {
    struct process *current = scheduler_get_current();
    if (current && current->interrupt_frame) {
        // Preemptive tasks cannot call yield()
        return;
    }
    // ... existing yield() logic
}
```

### Solution Option B: Unified stack frame
Make all tasks use interrupt frame (more complex).

### Status
- **DONE**: `yield()` returns without scheduling when the current task uses an interrupt frame
- **Test**: Covered by `make test-scheduler-safety` marker `SCHED_YIELD_GUARD_OK`

---

## Bug #5: Paging Allocator 4MB Limit

### Problem
- `paging_alloc_frame()` calls `frame_alloc_below(0x00400000)`
- Restricts page tables to first 4MB (identity-mapped region)
- Limits address spaces to ~128

### Solution
This is architectural. Options:
1. **Document the limit** (quick fix)
2. **Implement recursive page directory mapping** (proper fix, complex)

For now: Document it clearly.

### Status
- **DOCUMENTED**: Current page-table allocation still depends on low identity-mapped frames
- **FUTURE**: Implement recursive mapping when needed

---

## Bug #6: Broken Shell Reboot Command

### Problem
- `reboot` command does `int $0x03` expecting triple fault
- Vector 3 not registered → GPF → panic handler → hang
- System doesn't actually reboot

### Solution
Proper triple fault or keyboard controller reset:

```c
void shell_reboot(void) {
    serial_puts("Rebooting via keyboard controller...\n");
    
    // Wait for input buffer empty
    while (inb(0x64) & 0x02);
    
    // Send reset command
    outb(0x64, 0xFE);
    
    // If that fails, try triple fault
    asm volatile(
        "cli\n"
        "lidt 0\n"  // Load null IDT
        "int $0x03\n"  // Trigger interrupt
    );
    
    while (1) { asm volatile("hlt"); }
}
```

### Status
- **DONE**: Shell reboot uses keyboard-controller reset with a triple-fault fallback
- **Test**: Manual shell reboot validation remains separate from automated boot gates

---

## Test Plan

Current regression gates:
```bash
make test
make test-deep
make test-scheduler-safety
```

## Remaining Priority Order

1. **Bug #3** (default exception handlers) - improves debuggability for uncommon CPU exceptions
2. **Bug #5** (page-table allocator 4MB limit) - architectural cleanup when process/address-space scale grows
3. **Core stress** - repeat process/syscall/scheduler/paging paths under larger lifecycle tests
