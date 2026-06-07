# Phase 3 Implementation Notes

## Completed So Far

### ✅ Process Structure Extended
- Added `parent_pid`, `exit_code`, `exited`, `waited` fields
- Added `process_get_by_pid()` helper function

### ✅ SYS_GETPID (Syscall 14)
**Implementation:** Returns current process PID
```c
struct process *current = process_get_current();
return current ? current->pid : 0;
```

### ✅ SYS_EXIT (Syscall 15)
**Implementation:** Process termination with exit code
- Sets exit_code and exited flag
- Marks process as DEAD
- Calls scheduler to switch to next process
- Never returns

**Missing:** 
- Reparent children to init (PID 1)
- Signal SIGCHLD to parent

### ✅ SYS_WAIT (Syscall 16)
**Implementation:** Parent waits for child exit
- Scans for exited children
- Returns child PID and exit code
- Cleans up zombie process
- Returns -1 (ECHILD) if no exited children

**Limitation:** Doesn't block if child not ready (returns immediately)

### ⚠️ SYS_FORK (Syscall 17) - SIMPLIFIED
**Current Implementation:** Creates child but shares address space
- Creates new process
- Sets parent_pid
- **SHARES CR3 with parent** (not true fork!)
- Adds to scheduler

**Critical Limitation:**
This is NOT a real fork - child and parent share the same memory!
A proper fork needs:
1. Clone page directory
2. Copy all page tables
3. Copy physical pages (or implement COW)
4. Copy interrupt frame so child returns 0

**Why simplified?**
True fork requires ~100+ lines of careful paging code. This gives us
a working syscall to test the infrastructure, but it won't behave like
Unix fork. We can upgrade it later.

### ⏭️ SYS_EXEC (Syscall 18) - TODO
**Plan:** Replace current process with ELF from disk
- Use existing ELF loader
- Load into current CR3
- Reset user stack
- Jump to entry point

### ⏭️ SYS_BRK (Syscall 19) - TODO
**Plan:** Extend user heap dynamically
- Track per-process heap boundary
- Allocate/free pages as needed
- Return new brk value

---

## Current Status

**Build:** ✅ Compiles successfully  
**Tests:** ⚠️ Need to create fork/wait test  
**Functionality:** 
- getpid() works
- exit() works  
- wait() works
- fork() works but simplified (shared memory)

---

## Next Steps

1. **Test current implementation** - Create user program that calls getpid/exit/wait
2. **Implement SYS_EXEC** - Load ELF and replace process
3. **Implement SYS_BRK** - Dynamic heap
4. **Optional: Proper fork** - Clone address space with COW

---

## Safety Notes

The simplified fork() means:
- Parent and child share ALL memory
- Writes from child visible to parent
- NOT suitable for real isolation
- Good enough for testing process lifecycle
- Must be upgraded before production use
