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

Next hardening:
- Add argv/envp setup, per-process fd tables, and stricter lifecycle ownership once process management grows beyond the current single-running-process tests.

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

## RT-EXEC-002

Goal: stop file capabilities from leaking across process image replacement while the OS still lacks per-process fd tables and close-on-exec flags.

Implemented control:
- Successful `SYS_EXEC` calls `vfs_close_all()` after loading the new image and before returning to ring 3.
- The red probe opens a writable fd before exec; the new ELF entry expects `SYS_WRITE` on that inherited fd to return `SYSCALL_EBADF`.

Verification gate:
- `make test-red-team` proves the inherited-fd attack is blocked.
- `make test-process-syscall` must still prove normal VFS-backed ELF entry transfer.

Next hardening:
- Replace the global VFS descriptor table with per-process fd tables and explicit close-on-exec policy.

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
- Add real zombie reaping and exit-path cleanup gates once blocked `wait` and process-parent ownership become first-class behavior.

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
