# Patch Playbooks

## RT-HARNESS-001

Goal: stop user-mode code from forging trusted harness markers.

Implemented control:
- `SYS_TEST_MARKER` requires `SYSCALL_TEST_MARKER_TOKEN`.
- The red probe attempts the old no-token marker forgery and fails the gate if the forged trusted marker appears.

Verification gate:
- `make test-red-team` proves the old no-token attack is blocked.
- Existing functional gates must still pass.

Next hardening:
- Replace static marker capabilities with per-test kernel-issued capabilities or remove user marker emission from trusted evidence entirely.

## RT-SYSCALL-001

Goal: keep diagnostic/test-only syscall surfaces from being callable by arbitrary ring-3 programs.

Implemented control:
- `SYS_TEST_ABI` now rejects ring-3 callers with `SYSCALL_EPERM`.
- The red probe calls `SYS_TEST_ABI` from ring 3 with the exact success argument pattern and fails if the ABI success marker can be forged.

Verification gate:
- `make test-red-team` proves the ring-3 test-only ABI marker path is blocked.
- `make test-syscall` must continue to prove the kernel-side syscall ABI register contract.

Next hardening:
- Move all marker-producing pseudo-syscalls behind compile-time selftest-only dispatch tables or replace marker syscalls with kernel-owned event buffers.

## RT-EXEC-001

Goal: make `SYS_EXEC` replace the process image instead of only changing the return EIP.

Implemented control:
- `SYS_EXEC` creates a fresh user page directory.
- ELF PT_LOAD segments are loaded into the fresh address space.
- A new user stack is installed.
- Heap start/end metadata is reset.
- CR3 is switched before returning to ring 3 at the new entry point.

Verification gate:
- `make test-red-team` proves the old heap sentinel is not accepted as a mapped user pointer after exec.
- `make test-process-syscall` must continue to prove ELF entry transfer.
- `make test-process-lifecycle` proves exec replaces the old mappings and resets heap state after fork/wait lifecycle activity.

Next hardening:
- Add waitpid options, minimal signal delivery, and pipes; COW/demand/guard ownership, argv/envp, and per-process descriptor tables are runtime-proven.

## RT-FS-001

Goal: prevent truncate/write loops from exhausting all remaining SimpleFS sectors for a small visible file.

Implemented control:
- SimpleFS maintains mounted-memory sector accounting rebuilt from the directory table.
- `O_TRUNC` returns old sectors to the free map.
- Growth allocation searches reusable contiguous runs before appending.
- Reallocation frees the old run after the new directory entry is written.

Verification gate:
- `make test-red-team` runs repeated truncate/write cycles and proves the filesystem reuses sectors.
- `make test-vfs` must remain green.

Next hardening:
- Persist the free map or add fsck-style recovery once persistence becomes a project claim.

## RT-FS-002

Goal: keep the filesystem namespace canonical and root-only until the OS deliberately implements directories.

Implemented control:
- SimpleFS rejects paths that do not start with `/`.
- SimpleFS rejects nested paths containing another `/`.
- SimpleFS rejects `/.` and `/..`.

Verification gate:
- `make test-red-team` proves relative, nested, and pseudo-special names are rejected.
- `make test-vfs` must still prove normal absolute root file creation, read/write, offsets, stat, and negative paths.

Next hardening:
- Add an explicit path canonicalizer when directories, delete, rename, or mount points become claimed features.

## RT-SCHED-001

Goal: prevent preemptive interrupt-frame tasks from entering cooperative context-switch code.

Implemented control:
- `yield()` returns without scheduling when the current process is backed by an interrupt frame.
- The red probe sets a preemptive task as current, enqueues another runnable preemptive task, calls `yield()`, and requires both current task and schedule count to remain unchanged.

Verification gate:
- `make test-red-team` proves the adversarial preemptive-yield mixing case is blocked.
- `make test-scheduler-safety` must continue to prove the functional yield guard, priority, fairness, and critical-section markers.

Next hardening:
- Split cooperative and interrupt-driven task types at the API level so invalid scheduling operations are harder to call accidentally.

## RT-EXEC-002

Goal: stop opted-in file capabilities from leaking across process image replacement while preserving ordinary descriptor inheritance semantics.

Implemented control:
- Successful `SYS_EXEC` calls `vfs_close_all()` after loading the new image and before returning to ring 3.
- The red probe opens a writable fd with `SYS_O_CLOEXEC`; the new ELF entry expects `SYS_WRITE` on that fd to return `SYSCALL_EBADF`.

Verification gate:
- `make test-process-fd` proves local ownership, real-fork inheritance/shared offsets, selective CLOEXEC, and exit cleanup.
- `make test-red-team` proves the CLOEXEC bypass attempt is blocked through actual exec.
- `make test-process-syscall` must still prove normal VFS-backed ELF entry transfer.

Next hardening:
- Keep process-local descriptor tables mapped to refcounted VFS open-file descriptions.
- Preserve descriptors across exec by default; close only `SYS_O_CLOEXEC` entries after the new image is fully prepared.

## RT-EXEC-003

Goal: make failed `SYS_EXEC` attempts atomic with respect to frame ownership.

Implemented control:
- `SYS_EXEC` destroys the fresh address space if ELF loading fails.
- `SYS_EXEC` destroys the fresh address space if user-stack allocation or validation fails.
- Successful `SYS_EXEC` switches to the new address space before reclaiming the old process address space.
- The red probe repeatedly runs malformed exec attempts and requires the free-frame count to remain stable.

Verification gate:
- `make test-red-team` proves failed exec attempts do not leak frames.
- `make test-process-syscall` must still prove valid VFS-backed ELF entry transfer.

Next hardening:
- Add targeted probes for mid-load allocation exhaustion and repeated successful exec chains.

## RT-PROC-001

Goal: make process destruction own and reclaim private address spaces.

Implemented control:
- `process_destroy()` calls `paging_destroy_address_space()` before clearing the process record.
- `paging_destroy_address_space()` ignores the kernel directory and the currently active directory, then reclaims private low tables, user page tables, mapped user frames, and the page directory frame.
- The red probe creates a process with a private address space and mapped user page, destroys it, and requires the free-frame count to return to baseline.

Verification gate:
- `make test-red-team` proves destroyed private process address spaces do not leak frames.
- `make test-process-syscall` must still prove process syscall and exec-entry behavior.

Next hardening:
- Add orphan/reparenting stress, `waitpid` options, and process-table exhaustion recovery now that blocking wait and zombie reaping have a runtime gate.

## RT-ELF-001

Goal: reject malformed ELF images that describe overlapping load page ranges before any user pages are mapped.

Implemented control:
- The ELF loader records page-aligned `PT_LOAD` ranges during validation.
- If a new segment overlaps any earlier load page range, the load returns `ELF_EINVAL` before mapping pages.
- The red probe verifies the rejected overlapping image leaves the target user page unmapped.

Verification gate:
- `make test-red-team` proves overlapping load ranges are blocked and do not leave partial mappings.
- `make test-elf-loader` must still prove valid ELF parsing, segment copying, BSS zero-fill, and invalid ELF rejection.
- `make test-process-syscall` must still prove VFS-backed ELF entry transfer.

Next hardening:
- Fuzz ELF headers, segment counts, range boundaries, and alignment combinations.

## RT-PROC-002

Goal: make fork failure atomic across process, kernel-stack, page-table, and user-frame ownership.

Implemented control:
- `paging_clone_directory()` unwinds every user frame and page table allocated before a clone failure.
- `process_fork()` releases the child kernel stack and process slot when address-space cloning fails.
- The red probe exhausts low frames, attempts fork, releases pressure, and requires exact frame and process-count recovery.

Verification gate:
- `make test-red-team` proves allocation-pressure fork failure does not leak resources or create a child.
- `make test-process-lifecycle` must continue to prove a successful fork gives parent/child return values, isolated memory, blocking wait, exit, and reap.

Next hardening:
- Add fault-injection points for each fork allocation step and repeated fork/exit/wait stress across process-table exhaustion.
## RT-TOOLING-001

Goal: make editor and static-analysis header resolution match the supported freestanding build.

Implemented control:
- Track `.vscode/c_cpp_properties.json` and `agentic-os.code-workspace`.
- Select `i686-elf-gcc`, `-ffreestanding`, and repository-local include roots.
- Compile default and all-feature branches with strict warnings-as-errors.
- Verify `string.h`, `elf.h`, `syscall.h`, and the cross-toolchain `stdint-gcc.h` through compiler `-H` traces.

Verification gate:
- `make test-static-analysis` proves supported compiler syntax, static checks, editor JSON, and header provenance.
- `make test-red-team-tooling` reproduces the unsupported host-header failure and proves the supported route is clean.

Next hardening:
- Generate `compile_commands.json` if the build adopts multiple per-file flag families.
- Keep absolute toolchain paths configurable when the project becomes portable across hosts.

## RT-HARNESS-002

Goal: require the address-space test marker to prove actual page-table state, not allocation success.

Implemented control:
- Activate each process CR3 and resolve the shared test virtual address.
- Require each resolved physical address to equal its intended frame before emitting `ADDRSPACE_MAP_OK`.
- Run isolation writes only after map verification.
- Restore the kernel CR3 and reclaim address spaces/frames on every setup outcome.

Verification gate:
- `make test-address-space` proves distinct physical mappings and isolated values at one shared virtual address.
- `make test-red-team-tooling` preserves the historical weak oracle and checks the current hardening.

Next hardening:
- Add deterministic allocation-failure injection for each address-space setup step.
- Expand the proof to user/supervisor and read/write permission differences across process directories.

## RT-HARNESS-003

Goal: make selftest markers process-owned, namespace-specific, one-shot evidence rather than a public-token protocol.

Implemented control:
- Remove the public token from the syscall ABI and authorize by the current process's kernel-owned marker mask.
- Compile marker and diagnostic syscall cases only into the selftest images that use them; the default syscall surface returns `SYSCALL_ENOSYS`.
- Grant each selftest only its own marker IDs, consume a permission on successful emission, copy remaining permissions on fork, preserve them on exec, and clear them on process destruction.
- Retain the retired-token, cross-namespace, and replay attacks as red-team regressions.

Verification gate:
- `make test-marker-surface` proves a near-default image has no marker handler.
- `make test-process-lifecycle` proves fork inheritance and exec preservation through child, parent, and replacement-image markers.
- `make test-red-team` proves retired-token, namespace-crossing, unauthorized, and replay attempts are blocked.

Next hardening:
- Extend marker grants only alongside new selftest namespaces; keep token replay probes active.

## RT-VM-001

Goal: prevent writable physical aliases after fork.

Implemented control:
- Two-phase clone allocates and validates all child tables before modifying the parent.
- Writable user PTEs become read-only `PAGE_COW` mappings in both address spaces.
- COW write faults copy only when the frame has multiple owners.

Verification gate:
- `make test-vm` proves shared state, split-on-write, and parent/child isolation.
- `make test-red-team` attempts a writable-alias bypass.

## RT-VM-002

Goal: make physical-frame ownership explicit and underflow-safe.

Implemented control:
- `frame_retain`, `frame_release`, and `frame_get_refcount` own every shared user mapping.
- `frame_free` remains a compatibility alias for release.
- Permanent low frames cannot transition below their reserved reference.

Verification gate:
- `make test-vm` proves exact frame accounting.
- `make test-red-team` attempts double release and underflow.

## RT-VM-003

Goal: keep one permanent unmapped page below every fixed user stack.

Implemented control:
- `USER_STACK_GUARD_BOTTOM..USER_STACK_BOTTOM` is never demand mapped.
- VM fault policy classifies the range before heap demand allocation.
- Fatal ring-3 faults exit only the current process; kernel faults still panic.

Verification gate:
- `make test-vm` proves the guard child terminates and its parent resumes.
- `make test-red-team` attempts direct guard classification bypass.

## RT-VM-004

Goal: preserve mappings and frame accounting when fault recovery cannot allocate.

Implemented control:
- Selftest images provide deterministic allocation-failure injection.
- COW PTE replacement occurs only after physical copy succeeds.
- Demand faults install a page only after both frame and page-table ownership succeed.
- Clone allocation is two-phase so pre-commit failure leaves parent PTEs unchanged.

Verification gate:
- `make test-vm` proves clone, COW, and demand OOM rollback.
- `make test-red-team` replays COW and demand rollback attacks.
