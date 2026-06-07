# Phase 3 Gap Analysis - Process Lifecycle

**Date:** 2026-06-07  
**Status:** Analyzing what's missing for Unix-like process model

---

## Current State: What EXISTS

### Syscalls Implemented (13 total)
```c
// Test/Debug syscalls
SYS_PUTS          // 1  - Print string to VGA
SYS_GETCHAR       // 2  - Read character from keyboard
SYS_CLEAR         // 3  - Clear VGA screen
SYS_UPTIME        // 4  - Get timer ticks
SYS_ECHO          // 5  - Echo back argument
SYS_TEST_ABI      // 6  - Test syscall ABI
SYS_USERMODE_TEST // 7  - Ring-3 validation
SYS_TEST_MARKER   // 8  - Test marker

// File I/O syscalls (complete)
SYS_OPEN          // 9  - Open file (VFS)
SYS_READ          // 10 - Read from fd
SYS_WRITE         // 11 - Write to fd
SYS_CLOSE         // 12 - Close fd
SYS_STAT          // 13 - Get file metadata
```

### Process Infrastructure
- `process_create()` - Creates new process (kernel-internal)
- `process_destroy()` - Terminates process
- `process_get_current()` - Get current process
- `scheduler` - Cooperative + preemptive scheduling
- `paging` - Per-process address spaces (CR3)
- Stack pool with free list (fixed Bug #1)

---

## What's MISSING: Unix Process Model

### Critical Gap: No Process Lifecycle Syscalls

The kernel can create processes internally, but **userspace has no way to create child processes**. Missing:

```c
// Process lifecycle (MISSING)
SYS_FORK     - Clone current process (copy address space)
SYS_EXEC     - Replace process image with ELF from disk
SYS_EXIT     - Terminate current process with exit code
SYS_WAIT     - Wait for child process to exit
SYS_GETPID   - Get current process ID
SYS_GETPPID  - Get parent process ID

// Memory management (MISSING)
SYS_BRK      - Extend heap boundary
SYS_MMAP     - Map memory region
SYS_MUNMAP   - Unmap memory region
```

### Missing Infrastructure

1. **Process Tree Management**
   - No parent-child relationships tracked
   - No process tree (parent/children pointers)
   - No orphan/zombie handling

2. **Exit Status Propagation**
   - No exit code storage
   - No wait queue for parent processes
   - No SIGCHLD equivalent

3. **Dynamic User Heap**
   - Current: Fixed heap location/size
   - Need: growable heap via brk/mmap

4. **Address Space Cloning (fork)**
   - Need: copy page directory
   - Optional: Copy-on-Write (COW) for efficiency
   - Frame allocator already exists (Bug fix phase)

5. **Process Replacement (exec)**
   - ELF loader exists ✓
   - Need: replace current process image
   - Need: preserve PID, file descriptors
   - Need: reset user stack/heap

---

## Implementation Plan: Phase 3 (P1)

### Priority 1: Basic Process Lifecycle (2-3 hours)

**Step 1: Add Process Tree Fields** (30 min)
```c
// In process.h
struct process {
    // ... existing fields ...
    uint32_t parent_pid;
    uint32_t *children;      // Array of child PIDs
    uint32_t child_count;
    uint32_t exit_code;
    int exited;              // Has process exited?
    int waited;              // Has parent waited?
};
```

**Step 2: Implement SYS_GETPID** (15 min)
```c
// Simplest syscall
uint32_t sys_getpid(void) {
    struct process *current = process_get_current();
    return current ? current->pid : 0;
}
```

**Step 3: Implement SYS_EXIT** (30 min)
```c
void sys_exit(uint32_t exit_code) {
    struct process *current = process_get_current();
    current->exit_code = exit_code;
    current->exited = 1;
    current->state = PROCESS_DEAD;
    
    // Reparent children to init (PID 1)
    for (int i = 0; i < current->child_count; i++) {
        // ... reparent logic
    }
    
    // Notify parent (future: SIGCHLD)
    // Schedule next process
    scheduler_schedule();
    
    // Never returns
    while (1) { asm volatile("hlt"); }
}
```

**Step 4: Implement SYS_WAIT** (45 min)
```c
uint32_t sys_wait(uint32_t *status) {
    struct process *current = process_get_current();
    
    // Find exited child
    for (int i = 0; i < current->child_count; i++) {
        struct process *child = process_get_by_pid(current->children[i]);
        if (child && child->exited && !child->waited) {
            if (status && is_user_pointer_valid((uint32_t)status, 4)) {
                *status = child->exit_code;
            }
            child->waited = 1;
            // Cleanup zombie
            return child->pid;
        }
    }
    
    // No exited children - block or return error
    return (uint32_t)-1;  // ECHILD
}
```

**Step 5: Implement SYS_FORK** (60 min)
```c
uint32_t sys_fork(void) {
    struct process *parent = process_get_current();
    
    // 1. Allocate new process
    struct process *child = process_create(0, 1);  // Entry doesn't matter
    if (!child) return (uint32_t)-1;
    
    // 2. Copy page directory (clone address space)
    child->cr3 = paging_clone_directory(parent->cr3);
    if (!child->cr3) {
        process_destroy(child);
        return (uint32_t)-1;
    }
    
    // 3. Copy parent's stack frame so child resumes at same point
    memcpy(child->kernel_stack, parent->kernel_stack, KERNEL_STACK_SIZE);
    child->esp = parent->esp;
    child->ebp = parent->ebp;
    
    // 4. Set up parent-child relationship
    child->parent_pid = parent->pid;
    // Add to parent's children array
    
    // 5. Child returns 0, parent returns child PID
    // (This is tricky - need to modify child's saved EAX in stack frame)
    
    scheduler_add(child);
    return child->pid;  // Parent sees child PID
}
```

**Step 6: Implement SYS_EXEC** (60 min)
```c
uint32_t sys_exec(const char *path) {
    if (!is_user_pointer_valid((uint32_t)path, 1)) {
        return SYSCALL_EFAULT;
    }
    
    // 1. Open ELF file from VFS
    struct file *f = vfs_open(path, 0);
    if (!f) return (uint32_t)-1;  // ENOENT
    
    // 2. Load ELF into current address space
    struct process *current = process_get_current();
    uint32_t entry = elf_load(f, current->cr3);
    if (!entry) {
        vfs_close(f);
        return (uint32_t)-1;
    }
    vfs_close(f);
    
    // 3. Reset user stack and heap
    // Clear old user memory, allocate fresh stack
    
    // 4. Jump to entry point in ring-3
    // Modify interrupt frame to return to new entry
    
    // Never returns to old code
}
```

### Priority 2: Dynamic Memory (1-2 hours)

**SYS_BRK Implementation** (60 min)
```c
uint32_t sys_brk(uint32_t new_brk) {
    struct process *current = process_get_current();
    
    // Validate and adjust heap boundary
    // Allocate/free pages as needed
    // Return new brk value
}
```

---

## Testing Strategy

### Test 1: Basic Process Lifecycle
```c
// Parent process
pid = fork();
if (pid == 0) {
    // Child
    exit(42);
} else {
    // Parent
    int status;
    wait(&status);
    // status should be 42
}
```

### Test 2: Exec
```c
// Replace current process with /bin/echo
exec("/bin/echo");
// Never reaches here
```

### Test 3: Fork + Exec (Shell Pattern)
```c
pid = fork();
if (pid == 0) {
    exec("/bin/ls");
} else {
    wait(NULL);
}
```

---

## Estimated Timeline

| Task | Time | Priority |
|------|------|----------|
| Process tree fields | 30 min | HIGH |
| SYS_GETPID | 15 min | HIGH |
| SYS_EXIT | 30 min | HIGH |
| SYS_WAIT | 45 min | HIGH |
| SYS_FORK | 60 min | HIGH |
| SYS_EXEC | 60 min | HIGH |
| SYS_BRK | 60 min | MEDIUM |
| Tests | 60 min | HIGH |
| **Total** | **5-6 hours** | |

---

## Dependencies

- ✅ Frame allocator (for page directory cloning)
- ✅ Paging infrastructure (per-process CR3)
- ✅ ELF loader (for exec)
- ✅ VFS (for exec file loading)
- ✅ Scheduler (for process switching)

**Everything needed already exists!** Just need to wire it together with syscalls.

---

## Next Session Goal

Implement the 6 critical syscalls:
1. SYS_GETPID (easiest)
2. SYS_EXIT
3. SYS_WAIT
4. SYS_FORK (hardest)
5. SYS_EXEC
6. SYS_BRK

This will unlock Unix-like process model and enable real userland programs.
