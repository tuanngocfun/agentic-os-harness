# 13 — Agent Routing And Risk

## Muc tieu

This file routes MiMo v2.5pro and other coding agents after the boot-to-shell milestone. It exists because the current OS has real boot/shell evidence and several targeted advanced-core proofs, but some kernel subsystems are still scaffold or partial. The next work must convert risky scaffolds into proven behavior, not add breadth.

Read this with `harness_profile.yaml`. If they conflict, fix the conflict before implementing code.

## Current Verdict

MiMo deserves to continue, but not with a wide-open "build the OS" mandate.

Proven now:
- Boot image builds from `boot.bin`, `stage2.bin`, `kernel.elf`, `kernel.bin`, and `os.img`.
- QEMU boot test proves `STAGE2_OK`, `BOOT_OK`, `KERNEL_INIT_OK`, and `SHELL_READY`. Every runtime gate requires explicit pinned `QEMU_BIOS_DIR` input and records fresh execution provenance in `build/qemu-runtime.jsonl`.
- Shell runtime test proves keyboard input, shell dispatch, and VGA output for `help`.
- Shell I/O test proves `echo ok` by requiring both the serial `SHELL_ECHO_OK` marker and a distinct VGA output line after the typed command.
- Timer selftest proves PIT-backed tick counter increments when `make test-timer` passes.
- Syscall ABI selftest proves the current `int 0x80` register contract when `make test-syscall` passes.
- Exception selftest proves divide-by-zero, invalid-opcode, GPF, and page-fault panic evidence as `KERNEL_PANIC:<vector>:<code>` when `make test-exception`, `make test-exception-div0`, `make test-exception-gpf`, and `make test-exception-pagefault` pass.
- Scheduler selftest proves queue rotation plus explicit cooperative context execution through `SCHED_A`, `SCHED_B`, and `SCHED_CONTEXT_OK`. Timer-preemption selftest proves IRQ0-driven switching between two non-yielding tasks through `PREEMPT_A`, `PREEMPT_B`, and `PREEMPT_OK`. Scheduler safety selftest proves current priority, fairness, and interrupt-critical-section markers through `SCHED_PRIORITY_OK`, `SCHED_FAIRNESS_OK`, and `SCHED_SAFETY_OK`.
- Paging selftest proves map/unmap, permission-bit bookkeeping, CR0.WP-backed supervisor write fault, unmap+invlpg fault evidence, and user/supervisor enforcement through the ring-3 page-fault gate. Address-space selftest proves distinct CR3 values and isolated physical frames behind the same virtual address.
- Syscall negative-path selftest proves ring-3 valid syscall execution and controlled errors for invalid syscall numbers, kernel-space pointers, and unmapped user pointers.
- Memory selftest proves E820-backed usable-memory detection for the configured 512 MiB QEMU run. E820/frame selftest proves E820 map handoff and physical frame allocation/free/reuse/exhaustion. Allocator selftest proves fixed-heap `kmalloc`/`kfree` allocation, reuse, free/coalescing accounting, and exhaustion.
- User-mode selftest proves a ring-3 transition and controlled user/supervisor page fault. It does not prove full userland.
- VFS and SimpleFS selftests prove a volatile root-only flat filesystem on ramdisk. File syscall selftest proves VFS-backed ring-3 open/read/write/close/stat. ELF loader prep selftest proves VFS-backed ELF32/i386 header validation, PT_LOAD materialization into user-mapped pages, BSS zero-fill, and negative paths. Process syscall selftest proves ring-3 `getpid`, dynamic `brk`, and VFS-backed exec entry. Process lifecycle selftest proves true fork parent/child return, private copied memory, blocking wait, child exit status, zombie reap, and exec image replacement. `make test-exec-args` proves bounded argc/argv/envp copy-in, transactional failure rollback, a 16-byte-aligned initial stack, and ring-3 content validation.
- Red/blue adversarial gate proves the current known attack catalog is blocked, including marker forgery, retired-token reuse, namespace crossing, one-shot replay, test-only syscall marker abuse, exec residual mapping and fd leaks, failed exec cleanup, process-destroy and fork-failure cleanup, SimpleFS exhaustion/namespace abuse, preemptive-yield mixing, and overlapping ELF segment rejection. This corrects the security posture without claiming broad hardening.

Not proven:
- Production-grade virtual memory with swap, automatic stack growth, or broad memory-safety assurance.
- Full userland syscall ABI coverage beyond the proven syscall set and negative paths.
- `waitpid` options, signals, pipes, swap, automatic stack growth, or SMP-safe lifecycle behavior.
- SMP-safe scheduling or production-grade synchronization.
- Persistent storage, networking, graphics, dynamic linking, and production-grade userland.
- Broad adversarial hardening beyond the currently blocked red-team catalog.

## Loop Traps Diagnosed

These were real current-state issues, not just reviewer anxiety:

1. Default `make test` was polluted by a syscall probe in `kernel_main()`. That made normal boot panic before `SHELL_READY`, so MiMo debugged boot/shell while the real cause was an advanced-core selftest running in the wrong route.
2. `scripts/boot_test.sh` treated only exact `KERNEL_PANIC` as a failure marker. Structured markers such as `KERNEL_PANIC:0x...` could be missed in evidence.
3. `scripts/exception_test.sh` could pass while reporting that no exception was triggered. A deep fault gate must fail unless it proves the intended fault path.
4. `scripts/shell_test.sh` overclaimed `echo ok` through QEMU monitor key injection. The monitor sequence was not stable enough for argument-bearing commands, and typed input (`> echo ok`) can be mistaken for command output (`ok`). The default shell route still claims only `help`; `make test-shell-io` is the separate proof for `echo ok`.
5. The regression contract checked repo-root files from the harness directory, which made kernel/script checks point at missing paths.
6. A 30-second boot-test timeout made a healthy live kernel look like an agent loop. The default timeout is now short; deeper liveness or soak tests should be explicit.
7. MiMo claimed scheduler context-switch proof while the selftest only checked saved `esp`. The corrected gate now requires queue rotation plus real task execution markers from two runnable contexts before claiming an explicit cooperative context switch.
8. MiMo's paging test could pass when no paging marker appeared; the corrected gate now requires `PAGING_MAP_OK`, `PAGING_UNMAP_OK`, `PAGING_PERM_OK`, `PAGING_WRITE_FAULT_OK`, `PAGING_UNMAP_FAULT_OK`, and `PAGING_OK`.
9. MiMo stopped after marking one subtask complete even though its own todo list still had pending P1/P2 work. A passing gate for one route is a partial handoff unless every pending task in `harness_profile.yaml` is either completed with evidence or explicitly left as next work.
10. MiMo re-entered the shell loop by adding optional `echo_rendered` evidence to `scripts/shell_test.sh`. Optional evidence that can be false while the script passes creates an ambiguous signal; default gates must either require a check or omit it entirely.
11. MiMo diagnosed a real Makefile class: `KERNEL_DEFINES` is not a source dependency. The current flag is `ENABLE_PAGING_SELFTEST`, not `CONFIG_PAGING_SELFTEST`; the fix is a `build/kernel_defines.stamp` dependency so plain `make all` rebuilds after selftest flags change.
12. Hardcoded floppy CHS geometry skipped kernel sectors when QEMU booted the image as a hard disk. The bootloader now queries BIOS geometry with `INT 13h AH=08h` and falls back to the old 18-sector/two-head geometry only if detection fails.

Fix pattern: keep normal boot/shell gates fast and stable; run risky subsystem probes only through explicit deep routes such as `make test-deep`.

## Progress Estimate

| Scope | Rating | Why |
|---|---:|---|
| Boot-to-shell teaching milestone | 80% | Build, boot markers, keyboard IRQ, shell dispatch, and VGA runtime proof pass. |
| Credible protected OS core | 82% | GDT/IDT, timer preemption, syscall negative paths, process lifecycle, per-process descriptors, paging faults/isolation, E820/frame lifecycle, allocator behavior, VFS, and ELF entry have targeted gates, but signals, pipes, waitpid options, swap, automatic stack growth, SMP safety, persistent storage, and full userland remain unproven. |
| Blended current project promise | 82% | Real stage-one implementation plus several stage-two proofs; remaining risk is deeper Unix-like process semantics and hardening, not storage or ELF-entry plumbing. |

## Routing Matrix

| Route | Allowed work | Required evidence | Do not claim |
|---|---|---|---|
| `harness_only` | Docs, route matrix, validation scripts, evidence templates | `check_harness_contract.sh`, `git_preflight.sh`, drift checks | Runtime OS behavior unless `make test` or a targeted gate proves it |
| `bootloader` | `boot/`, `Makefile`, linker/image layout, sector count | `make all`, `make test`, `boot.bin` 512 bytes, `stage2.bin` within 32 reserved sectors, kernel starts at LBA 33, `STAGE2_OK` before `BOOT_OK` | Kernel identity proof without a kernel magic/header check |
| `shell_runtime` | Shell, keyboard, VGA, shell test | `make test`, decoded VGA text has `Available commands:` from `help` | Argument-bearing shell commands such as `echo`, plus timer, memory, scheduler, syscall, or user mode |
| `shell_command_io` | Dedicated shell input/output proof only | `make test-shell-io` distinguishes typed command input from command output and requires `SHELL_ECHO_OK` | Default `scripts/shell_test.sh` evidence or substring matches such as `grep "echo ok"` |
| `syscall_abi` | `isr.asm`, `syscall.c`, syscall header, focused runtime test | Runtime test proves register ABI, return value, and failure syscall number | User-mode syscall unless ring-3 transition is also tested |
| `exception_panic` | IDT exception gates, panic output, negative test harness | Fault tests emit structured `KERNEL_PANIC:<vector>:<code>` and QEMU does not silently triple-fault | Stability of paging/user-mode until those tests exercise the path |
| `scheduler_queue` | Process creation, ready queue, scheduler tick/current-process APIs | Test proves deterministic queue rotation across two process records | Preemptive scheduling or isolation |
| `scheduler_truth` | PIT timer, scheduler, process, context switch asm/TSS | `make test-scheduler` proves cooperative context execution; `make test-timer-preemption` proves IRQ0-driven execution of two non-yielding tasks; `make test-scheduler-safety` proves current priority/fairness/critical-section markers | Process isolation or SMP-grade scheduling beyond the tested path |
| `paging_semantics` | Page tables, allocator for page tables, permissions, invalidation | Map/unmap, writable access, CR0.WP write fault, unmap+invlpg fault evidence, user/supervisor page fault, CR3 switching, same-VA/different-frame isolation, and no selftest flag leakage into default boot | Complete memory protection or frame lifecycle correctness |
| `virtual_memory` | Frame references, COW clone/faults, lazy heap, fixed stack guard, user-fault process exit | `make test-vm` plus lifecycle, paging, process, exec, and red-team regressions | Swap, automatic stack growth, broad memory safety, or SMP correctness |
| `memory_detection` | BIOS E820 detection, memory summaries, targeted memory/frame tests | `make test-memory` proves the configured QEMU usable-memory marker; `make test-e820-frame` proves E820 handoff and frame lifecycle; `make test-allocator` proves fixed-heap behavior | Production-grade heap growth or full VM memory-zone policy |
| `user_mode` | GDT/TSS selectors, user-mode entry, syscall/exception boundary | `make test-usermode` proves ring-3 transition and supervisor-page fault from user mode; address-space isolation is separately proven by `make test-address-space` | Userland ABI completeness or syscall negative-path coverage |
| `syscall_negative` | syscall validation, page-aware user pointer checks, ring-3 syscall harness | `make test-syscall-negative` proves invalid numbers, kernel pointers, unmapped user pointers, and valid ring-3 syscall marker path | Complete POSIX-style syscall ABI or resource-limit policy |
| `elf_loader_prep` | ELF parser/loader, VFS-backed file reads, user-page materialization | `make test-elf-loader` proves valid ELF32/i386 PT_LOAD copy, BSS zero-fill, metadata, and invalid/truncated/missing rejection | Dynamic linking or persistent storage |
| `process_syscall_elf_entry` | process syscall ABI, brk page lifecycle, VFS-backed exec entry transfer | `make test-process-syscall` proves ring-3 `getpid`, `brk` query/grow/read-write/shrink, no-child `wait`, and transfer into a tiny ELF entry | Fork/wait lifecycle semantics; argv/envp is separately proven by `make test-exec-args` |
| `process_lifecycle` | canonical interrupt frame, fork clone, scheduler blocking/exit, zombie reap, exec replacement, descriptor ownership | `make test-process-lifecycle`, `make test-exec-args`, plus `make test-process-fd` prove lifecycle, transactional argv/envp exec and process-local descriptor isolation, real-fork inheritance/shared offsets, CLOEXEC, and exit cleanup | COW/demand/guard and rollback are separately proven by `make test-vm`; `waitpid` options, signals, pipes, swap, and SMP safety remain unproven |
| `adversarial_red_team` | Guest-only probes that attempt known attacks against blue controls | `make test-red-team`/`make test-blue-team` prove known attacks are blocked and write `build/red-team/findings.jsonl` | Security certification, host-escape testing, or broader hardening than the catalog covers |

## Format Policy

Findings from `considerations/` are part of the routing contract:
- Keep agent instructions in concise Markdown: `AGENTS.md`, `README.md`, and `SKILL.md`.
- Keep single-source machine state in YAML: `harness_profile.yaml`.
- Keep runtime evidence machine-written as JSONL, not hand-edited prose verdicts.
- Use HTML only for rendered reports or dashboards, not as the primary harness input format.
- Prefer compass-style context: key files, commands, non-obvious risks, and verification gates over broad encyclopedia docs.

## Next MiMo Tasks

1. `waitpid-signals-pipes`
   Add waitpid options, minimal signal delivery, and pipes after VM ownership is strict.

2. `red-blue-fuzz-continuation`
   Keep expanding guest-only syscall, scheduler, paging, exec, and ELF probes after each feature lands.

## Forbidden Next Work

Do not add networking, graphics mode, or more shell commands before waitpid, signals, and pipes have targeted lifecycle gates. Existing filesystem and process-lifecycle claims remain limited to their named tests.

## External Practice Mapping

- Harness engineering: use feedforward guides and feedback sensors together; deterministic tests and linters should run early and continuously.
- Anthropic agent guidance: keep tool interfaces simple, documented, and tested; use evaluator-style feedback loops only when the evaluator has a real signal.
- OpenAI agent guidance: use traces/evals/guardrails so failures are visible at workflow level, not hidden behind final success text.
- Tesla Fleet Telemetry: issue/PR workflow and "Ready for review" context make changes reviewable; their telemetry docs also model explicit prerequisites, state transitions, and failure behavior.
- Netflix PR confidence: compare failing signals against baseline/destination-branch behavior instead of trusting a raw red/green result.
- Apple kernel guidance: keep interrupt-context work tiny and bounded; shared state needs deliberate synchronization.
- Microsoft driver guidance: kernel code needs static analysis, runtime verifier-style checks, fuzzing/corner cases, and expert review because kernel mistakes affect the whole OS.
- TencentOS Server: production OS value is stability, security, performance, lifecycle, and large-scale validation; feature count is not enough.
- Meta DRS: high-risk diffs deserve deeper gates and clearer risk explanations.
- Meta standardized-agent practice: use shared tools plus domain skills; do not let agents guess when a compact context file can route them.
- Meta bug-catcher practice: turn known failure modes into tests that must catch the fault, not only prove the happy path.
- Google code review: prefer small self-contained changes with tests; accept improvements, but never merge changes that degrade code health.
- Applied routing rule from these practices: turn recurring debug loops into deterministic regression checks, keep default gates single-purpose, and move risky/flaky probes into explicit targeted routes with unambiguous evidence.

## Handoff Template

Each MiMo handoff must answer:
- Route used:
- Files touched:
- Claim being made:
- Claim status before/after:
- Pending tasks remaining:
- Handoff status: complete for this route, or partial with next route:
- Fast gates run:
- Deep gates run:
- Evidence artifacts:
- Remaining unproven behavior:
- Rollback path:
## Static Integrity Route

Use `make test-static-analysis` whenever C headers, compiler flags, assembly ABI declarations, selftest branches, shell gates, or editor configuration change. Use `make test-red-team-tooling` when a reported defect may be caused by tooling drift or a weak test oracle. Finish the evidence cycle with `make test-meta-loop` and an evidence-scoped patch release.
