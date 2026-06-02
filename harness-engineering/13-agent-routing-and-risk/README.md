# 13 — Agent Routing And Risk

## Muc tieu

This file routes MiMo v2.5pro and other coding agents after the boot-to-shell milestone. It exists because the current OS has real boot/shell evidence, but advanced kernel subsystems are still scaffold or partial. The next work must convert risky scaffolds into proven behavior, not add breadth.

Read this with `harness_profile.yaml`. If they conflict, fix the conflict before implementing code.

## Current Verdict

MiMo deserves to continue, but not with a wide-open "build the OS" mandate.

Proven now:
- Boot image builds from `boot.bin`, `kernel.elf`, `kernel.bin`, and `os.img`.
- QEMU boot test proves `BOOT_OK`, `KERNEL_INIT_OK`, and `SHELL_READY`.
- Shell runtime test proves keyboard input, shell dispatch, and VGA output for `help`.
- Syscall ABI selftest proves the current `int 0x80` register contract when `make test-syscall` passes.
- Exception selftest proves divide-by-zero, invalid-opcode, GPF, and page-fault panic evidence as `KERNEL_PANIC:<vector>:<code>` when `make test-exception`, `make test-exception-div0`, `make test-exception-gpf`, and `make test-exception-pagefault` pass.
- Scheduler selftest proves only queue rotation through `scheduler_tick()` and `scheduler_get_current()`, not context switching.
- Paging selftest proves only map/unmap bookkeeping plus writable access for one mapped page, not protection or isolation.

Not proven:
- Timer-driven scheduler/context switching.
- Ring-3 user-mode transition.
- Real paging protection, permissions, page-fault behavior, and isolation.
- Memory allocator or detected memory map.

## Loop Traps Diagnosed

These were real current-state issues, not just reviewer anxiety:

1. Default `make test` was polluted by a syscall probe in `kernel_main()`. That made normal boot panic before `SHELL_READY`, so MiMo debugged boot/shell while the real cause was an advanced-core selftest running in the wrong route.
2. `scripts/boot_test.sh` treated only exact `KERNEL_PANIC` as a failure marker. Structured markers such as `KERNEL_PANIC:0x...` could be missed in evidence.
3. `scripts/exception_test.sh` could pass while reporting that no exception was triggered. A deep fault gate must fail unless it proves the intended fault path.
4. `scripts/shell_test.sh` overclaimed `echo ok` through QEMU monitor key injection. The monitor sequence was not stable enough for argument-bearing commands, so the harness now claims only `help` dispatch until a better input path exists.
5. The regression contract checked repo-root files from the harness directory, which made kernel/script checks point at missing paths.
6. A 30-second boot-test timeout made a healthy live kernel look like an agent loop. The default timeout is now short; deeper liveness or soak tests should be explicit.
7. MiMo claimed scheduler context-switch proof while the selftest only printed `SCHED_A` and `SCHED_B`; the corrected gate now requires real queue rotation and refuses to call it a context switch.
8. MiMo's paging test could pass when no paging marker appeared; the corrected gate now requires `PAGING_MAP_OK`, `PAGING_UNMAP_OK`, and `PAGING_OK`.
9. MiMo stopped after marking one subtask complete even though its own todo list still had pending P1/P2 work. A passing gate for one route is a partial handoff unless every pending task in `harness_profile.yaml` is either completed with evidence or explicitly left as next work.

Fix pattern: keep normal boot/shell gates fast and stable; run risky subsystem probes only through explicit deep routes such as `make test-deep`.

## Progress Estimate

| Scope | Rating | Why |
|---|---:|---|
| Boot-to-shell teaching milestone | 80% | Build, boot markers, keyboard IRQ, shell dispatch, and VGA runtime proof pass. |
| Credible protected OS core | 35% | GDT/IDT exist; syscall ABI, one panic path, scheduler queue rotation, and paging map/unmap bookkeeping have targeted gates, but user mode, context switching, protection, and broader fault coverage are still unproven. |
| Blended current project promise | 60% | Real stage-one implementation plus risky stage-two scaffolding. |

## Routing Matrix

| Route | Allowed work | Required evidence | Do not claim |
|---|---|---|---|
| `harness_only` | Docs, route matrix, validation scripts, evidence templates | `check_harness_contract.sh`, `git_preflight.sh`, drift checks | Runtime OS behavior unless `make test` or a targeted gate proves it |
| `bootloader` | `boot/`, `Makefile`, linker/image layout, sector count | `make all`, `make test`, `boot.bin` 512 bytes, generated `KERNEL_SECTORS <= 120` | Kernel identity proof without a kernel magic/header check |
| `shell_runtime` | Shell, keyboard, VGA, shell test | `make test`, decoded VGA text has `Available commands:` from `help` | Argument-bearing shell commands such as `echo`, plus timer, memory, scheduler, syscall, or user mode |
| `syscall_abi` | `isr.asm`, `syscall.c`, syscall header, focused runtime test | Runtime test proves register ABI, return value, and failure syscall number | User-mode syscall unless ring-3 transition is also tested |
| `exception_panic` | IDT exception gates, panic output, negative test harness | Fault tests emit structured `KERNEL_PANIC:<vector>:<code>` and QEMU does not silently triple-fault | Stability of paging/user-mode until those tests exercise the path |
| `scheduler_queue` | Process creation, ready queue, scheduler tick/current-process APIs | Test proves deterministic queue rotation across two process records | CPU context switch, task execution, preemptive scheduling, or isolation |
| `scheduler_truth` | PIT timer, scheduler, process, context switch asm/TSS | Test proves at least two runnable contexts execute in expected order | Process isolation or preemptive multitasking without context-save proof |
| `paging_semantics` | Page tables, allocator for page tables, permissions, invalidation | Map/unmap bookkeeping, writable access, and later page-fault/permission tests | Memory protection until page-fault and user/supervisor permission tests pass |

## Format Policy

Findings from `considerations/` are part of the routing contract:
- Keep agent instructions in concise Markdown: `AGENTS.md`, `README.md`, and `SKILL.md`.
- Keep single-source machine state in YAML: `harness_profile.yaml`.
- Keep runtime evidence machine-written as JSONL, not hand-edited prose verdicts.
- Use HTML only for rendered reports or dashboards, not as the primary harness input format.
- Prefer compass-style context: key files, commands, non-obvious risks, and verification gates over broad encyclopedia docs.

## Next MiMo Tasks

1. `syscall-abi-proof`  
   Fix the current register mismatch risk first. The sample caller places data in `ebx`, while the stub passes `eax/ecx/edx/esi` to C. Add a tiny test syscall that returns known register values or prints a deterministic marker only if the ABI is correct.

2. `exception-panic-path`  
   Add exception handlers for divide-by-zero, invalid opcode, general protection fault, and page fault. Required evidence is a structured serial panic marker and a negative QEMU test that fails if the machine just exits, reboots, or hangs silently.

3. `scheduler-truth`  
   Current proof is only queue rotation. To claim scheduler truth, wire a timer-driven scheduling path or safe context-switch harness and prove two contexts actually execute in a deterministic order.

4. `paging-semantics`  
   Current proof is only map/unmap bookkeeping plus writable access. Add invalidation, permission, and page-fault tests before any isolation or memory-protection claim.

5. `evidence-unification`  
   Make `scripts/shell_test.sh` append structured JSONL evidence like `scripts/boot_test.sh`, so the harness can compare boot, shell, and future deep gates uniformly.

6. `shell-command-io-proof`  
   Add a stable input path before claiming argument-bearing shell commands. QEMU monitor `sendkey` proved `help`, but it produced unreliable ordering for `echo ok`; do not use a flaky monitor sequence as proof.

## Forbidden Next Work

Do not add filesystem, networking, graphics mode, or more shell commands before the P0/P1 items above. Those would increase apparent progress while leaving the most severe hidden-bug surface untouched.

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
