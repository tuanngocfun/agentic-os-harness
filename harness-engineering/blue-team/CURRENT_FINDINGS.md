# Current Findings

## RT-HARNESS-001

- Impact: Ring-3 code can forge trusted success markers through `SYS_TEST_MARKER`, weakening marker-based runtime evidence.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_MARKER_FORGERY_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: `SYS_TEST_MARKER` now requires a test-only marker capability; no-capability user attempts are rejected.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-SYSCALL-001

- Impact: test-only syscall surfaces can let ring-3 programs forge runtime evidence or trigger privileged diagnostic side effects outside the intended selftest route.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_SYSCALL_PRIVILEGE_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: `SYS_TEST_ABI` is kernel-selftest-only; ring-3 attempts with the success argument pattern return `SYSCALL_EPERM`.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-EXEC-001

- Impact: `SYS_EXEC` transfers control into a new ELF without full address-space replacement, so old heap mappings can survive into the new image.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_EXEC_RESIDUAL_MAPPING_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: `SYS_EXEC` builds a fresh user address space, loads the ELF into it, installs a new user stack, resets heap metadata, and switches CR3 before returning to ring 3.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-FS-001

- Impact: repeated truncate/write cycles can consume ramdisk data sectors without growing the visible file, creating a SimpleFS denial of service inside the guest.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_SIMPLEFS_DOS_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: SimpleFS now rebuilds mounted-memory sector accounting from directory metadata and returns old extents on truncate/reallocation.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-FS-002

- Impact: relative or pseudo-special names can create namespace aliases even though SimpleFS only claims root-only absolute paths.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_VFS_NAMESPACE_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: SimpleFS now requires `/name` root paths, rejects nested paths, and rejects `/.` plus `/..`.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-SCHED-001

- Impact: preemptive tasks with interrupt-frame stacks can corrupt scheduler state if they enter the cooperative `yield()` path.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_SCHED_YIELD_MIXING_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: `yield()` returns immediately when the current process is interrupt-frame based, and the adversarial probe verifies that current task and schedule count remain unchanged.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-EXEC-002

- Impact: a pre-exec writable VFS descriptor can survive into a new executable image and leak authority across program replacement.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_EXEC_FD_LEAK_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: successful `SYS_EXEC` closes all current VFS descriptors before switching to the new image.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-EXEC-003

- Impact: repeated failed `SYS_EXEC` attempts can leak address-space frames and become a guest-triggered memory exhaustion path.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_EXEC_FAILURE_CLEANUP_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: `SYS_EXEC` now destroys uncommitted address spaces on ELF-load or stack-setup failure and reclaims the replaced old address space after successful exec.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-PROC-001

- Impact: destroying a process that owns a private address space can leak page-directory, page-table, and mapped user frames.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_PROCESS_DESTROY_CLEANUP_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: `process_destroy()` now calls `paging_destroy_address_space()` before clearing the process record; kernel/current directories are protected by the paging destructor.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-ELF-001

- Impact: malformed ELF files with overlapping `PT_LOAD` page ranges can force a failed load after partial page mapping, leaving unexpected user mappings behind.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_ELF_OVERLAP_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: ELF loading now validates all page-aligned load ranges for overlap before mapping any segment pages.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.

## RT-PROC-002

- Impact: a fork failure during address-space cloning can leak frames, kernel stacks, or process slots and become a guest-triggered resource exhaustion path.
- Attack/defense gate: `make test-red-team`
- Expected marker: `RED_FORK_FAILURE_CLEANUP_BLOCKED`
- Machine evidence: `build/red-team/findings.jsonl`
- Blue control: fork creation is transactional; paging clone unwinds partially allocated user frames and page tables, while process creation releases the child stack and record when cloning fails.
- Verification gate: `make test-red-team`

Status: blocked by current blue control; keep regression probe active.
