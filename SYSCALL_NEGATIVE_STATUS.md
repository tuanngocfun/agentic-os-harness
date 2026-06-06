# Syscall Negative Path Validation

## Status: COMPLETED (2026-06-06)

## Implementation

Added comprehensive syscall error handling and validation:

### 1. **Error Codes** (`include/syscall.h`)
```c
#define SYSCALL_SUCCESS     0
#define SYSCALL_EINVAL      ((uint32_t)-1)  // Invalid argument
#define SYSCALL_EFAULT      ((uint32_t)-2)  // Bad address
#define SYSCALL_EPERM       ((uint32_t)-3)  // Permission denied
#define SYSCALL_ENOSYS      ((uint32_t)-4)  // Invalid syscall number
```

### 2. **Validation Functions** (`kernel/syscall.c`)

**Invalid Syscall Numbers:**
- Validates syscall_num is in range [1, SYS_MAX]
- Returns `SYSCALL_ENOSYS` for out-of-range calls
- Checks both zero and values > SYS_MAX

**Pointer Validation:**
- User-space range: `0x40000000 - 0xC0000000`
- Validates pointer is within user-space
- Checks for overflow on pointer + size
- Applied to `SYS_PUTS` string argument

**String Length Protection:**
- Limits string length to 4096 bytes
- Prevents runaway reads
- Validates null termination

**Ring Privilege Check:**
- Validates caller CS register: `(cs & 0x03) == 0x03`
- Applied to `SYS_USERMODE_TEST`
- Returns `SYSCALL_EPERM` for ring 0 calls

### 3. **Test Coverage** (`scripts/syscall_negative_test.sh`)

Tests validate four negative paths:
1. **Invalid syscall number (999)** → `SYSCALL_ENOSYS`
2. **Invalid syscall number (0)** → `SYSCALL_ENOSYS`
3. **Bad pointer (kernel space 0x00100000)** → `SYSCALL_EFAULT`
4. **Ring check (ring 0 calling SYS_USERMODE_TEST)** → `SYSCALL_EPERM`

Required markers:
- `SYSCALL_NEGATIVE_TEST`
- `SYSCALL_INVALID_NUM_OK`
- `SYSCALL_BAD_POINTER_OK`
- `SYSCALL_RING_CHECK_OK`
- `SYSCALL_NEGATIVE_OK`

## Validation

```bash
make test-syscall-negative
```

## Integration

- Added to `make test-deep`
- Test runs with `ENABLE_SYSCALL_NEGATIVE_SELFTEST` define
- Comprehensive error code coverage

## What's Still Missing

These are acknowledged as still pending:

1. **More comprehensive pointer validation:**
   - Page-level validation (check page table present bit)
   - Length validation for all syscall arguments
   - Recursive validation for pointer-to-pointer cases

2. **Resource limit checks:**
   - File descriptor validation
   - Buffer size limits
   - Rate limiting

3. **Additional negative paths:**
   - Concurrent syscall stress testing
   - Edge case argument values (INT_MAX, etc.)
   - Malformed data structures

## Related Files

- `kernel/syscall.c` - Validation implementation
- `include/syscall.h` - Error codes and user-space range
- `kernel/kernel.c` - Selftest implementation
- `scripts/syscall_negative_test.sh` - Test script
- `Makefile` - test-syscall-negative target
