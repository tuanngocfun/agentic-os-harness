# Attack Catalog

The catalog tracks current adversarial assumptions. Each item has a matching blue-team finding and patch playbook.

## RT-HARNESS-001: Ring-3 Test Marker Forgery

- Subsystem: syscall test harness
- Severity: high
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_MARKER_FORGERY_BLOCKED`
- Finding log ID: `RT-HARNESS-001`

Ring-3 code previously could call `SYS_TEST_MARKER` and emit trusted harness markers. The current probe attempts the old no-capability marker forgery and expects `SYSCALL_EPERM`; if the old trusted marker appears, the gate fails.

## RT-SYSCALL-001: Test-Only Syscall Surface Fuzzing

- Subsystem: syscall test-only surface
- Severity: medium
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_SYSCALL_PRIVILEGE_BLOCKED`
- Finding log ID: `RT-SYSCALL-001`

Test-only syscall entry points can become marker forgery or privileged side-effect paths if they stay reachable from arbitrary ring-3 code. The current probe calls the ABI marker syscall from ring 3 with the exact success arguments and expects `SYSCALL_EPERM`.

## RT-EXEC-001: Exec Residual Mapping

- Subsystem: process exec
- Severity: high
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_EXEC_RESIDUAL_MAPPING_BLOCKED`
- Finding log ID: `RT-EXEC-001`

`SYS_EXEC` previously loaded a VFS-backed ELF and redirected the saved interrupt return EIP without replacing the process address space. The current probe writes a sentinel into the old process heap, calls `SYS_EXEC`, and the new ELF entry verifies the old heap pointer is rejected by syscall pointer validation.

## RT-FS-001: SimpleFS Truncate/Write Exhaustion

- Subsystem: SimpleFS
- Severity: medium
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_SIMPLEFS_DOS_BLOCKED`
- Finding log ID: `RT-FS-001`

SimpleFS v1 previously used contiguous append-only allocation. The current probe repeatedly truncates and rewrites one file and expects the filesystem to reuse the same sector run instead of exhausting the ramdisk.

## RT-FS-002: VFS Namespace Alias Abuse

- Subsystem: SimpleFS namespace
- Severity: medium
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_VFS_NAMESPACE_BLOCKED`
- Finding log ID: `RT-FS-002`

SimpleFS initially accepted relative names such as `relative` even though the claimed namespace is root-only absolute paths such as `/hello.txt`. The current probe attempts relative, nested, `.`, and `..` names and expects `VFS_EINVAL` so aliases do not become future traversal or policy-bypass hooks.

## RT-SCHED-001: Preemptive Yield Mixing

- Subsystem: scheduler preemption boundary
- Severity: medium
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_SCHED_YIELD_MIXING_BLOCKED`
- Finding log ID: `RT-SCHED-001`

Preemptive tasks use interrupt-frame stacks and must not enter the cooperative `yield()` path, which expects a different context layout. The current probe sets a preemptive task as current, places another runnable task in the ready queue, calls `yield()`, and expects no scheduling or context switch to occur.

## RT-EXEC-002: Exec File Descriptor Leak

- Subsystem: process exec / VFS descriptor table
- Severity: high
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_EXEC_FD_LEAK_BLOCKED`
- Finding log ID: `RT-EXEC-002`

The current OS still has a global VFS descriptor table, so an old process image could leave a writable descriptor open across `SYS_EXEC`. The current probe opens a file before exec, then the new ELF image attempts to write through the inherited fd and expects `SYSCALL_EBADF`.

## RT-EXEC-003: Failed Exec Resource Leak

- Subsystem: process exec failure cleanup
- Severity: medium
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_EXEC_FAILURE_CLEANUP_BLOCKED`
- Finding log ID: `RT-EXEC-003`

`SYS_EXEC` allocates a fresh address space before it knows whether the target ELF can be loaded. Without rollback on every failure path, repeated invalid exec attempts can leak page directories, low page tables, segment pages, or stack frames. The current probe repeatedly executes a malformed ELF and expects the free-frame count to remain unchanged.

## RT-PROC-001: Process Destroy Address-Space Leak

- Subsystem: process lifecycle cleanup
- Severity: medium
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_PROCESS_DESTROY_CLEANUP_BLOCKED`
- Finding log ID: `RT-PROC-001`

`process_destroy()` previously released the kernel stack and cleared the process record, but did not reclaim a private address space owned by the process. The current probe gives a process a private page directory plus a mapped user page, destroys the process, and expects the free-frame count to return to its exact baseline.

## RT-ELF-001: Overlapping ELF Load Segments

- Subsystem: ELF loader
- Severity: high
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_ELF_OVERLAP_BLOCKED`
- Finding log ID: `RT-ELF-001`

Malformed ELF files can describe multiple `PT_LOAD` ranges that map the same user page. Without whole-image overlap validation, a loader can map the first segment, reject the second, and leave partial user mappings behind after a failed load. The current probe writes an overlapping-segment ELF, expects `ELF_EINVAL`, and verifies the target user page was not mapped.

## RT-PROC-002: Fork Clone Failure Cleanup

- Subsystem: process fork / paging clone rollback
- Severity: high
- Attack/defense gate: `make test-red-team`
- Evidence marker: `RED_FORK_FAILURE_CLEANUP_BLOCKED`
- Finding log ID: `RT-PROC-002`

Fork clones a process record, kernel stack, page directory, page tables, and user frames. A mid-clone allocation failure can leak any partially owned resource or leave a phantom child. The current probe exhausts the low-frame pool, attempts `process_fork()`, requires failure, then verifies both frame accounting and process count return to their exact baselines.
