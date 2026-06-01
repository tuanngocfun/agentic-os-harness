# 05 — Sub-Agents

## Mục tiêu

Sub-agents tạo role separation: writer viết, reviewer đọc, tester chạy tests, debugger phân tích log. Đây là cách giảm drift và giảm side effects khi task dài.

Important: role separation is only a documented contract until platform-specific agent files, wrappers, or tool policies enforce it. Do not claim reviewer/tester/debugger permissions are enforced unless `.agent/agents/` or platform equivalents exist and validation checks them.

## Role Matrix

| Agent | Mission | Write? | Bash? | Must read |
|---|---|---:|---|---|
| `orchestrator` | Break down tasks, assign roles, merge results | Yes | Build/test only | `AGENTS.md`, `llms.txt` |
| `code-writer` | Implement focused boot/kernel/doc changes | Yes | Build only | Relevant skill + target files |
| `code-reviewer` | Find bugs, contradictions, missing tests | No | Read-only | Changed files + references |
| `test-runner` | Run build/test and parse serial markers | No | Test only | Makefile, `boot_test.sh` |
| `debugger` | Diagnose boot failures from logs | No | Read-only | `build/serial.log`, known issues |
| `safety-reviewer` | Check host/QEMU safety constraints | No | Read-only | safety docs, QEMU commands |
| `critic` | Stress-test assumptions and find missing edge cases | No | Read-only/test only | validation docs, changed files |
| `auditor` | Verify authenticity: no fake pass, no hardcoded marker cheat | No | Read-only | logs, artifacts, diffs |
| `sentinel` | Watch long runs for stale progress and blocked loops | No | Read-only/process monitor | progress/evidence logs |
| `doc-maintainer` | Keep docs/index/memory consistent | Yes | Search only | `llms.txt`, section READMEs |
| `git-steward` | Inspect repo state, diff, tracked artifacts, and prepare handoff/staging plan | No by default | Read-only Git | `12-git-change-management/README.md`, `.gitignore`, changed files |

## Agent Contracts

### `orchestrator`

- Splits large OS work into build, marker, validation, memory, and doc tasks.
- Cannot mark a phase complete without test-runner evidence.
- Must record assumptions if a feature is optional or not implemented yet.

### `code-writer`

- Writes only the files assigned by orchestrator.
- Must preserve public artifact contract.
- Must not broaden scope from boot harness into production OS claims.

### `code-reviewer`

- Read-only.
- Reports findings first, ordered by severity.
- Checks for stale patterns: `boot.o` as disk sector, missing `objcopy`, VGA-only markers, unsafe QEMU commands.

### `test-runner`

- Runs `make all` and `make test` when source tree exists.
- Parses `build/serial.log` for required markers with exact whole-line matching.
- Treats timeout `124` as the normal live-kernel pass status after marker parsing. Treats early QEMU exit `0` as failure unless the test is explicitly a shutdown test.
- Reports exact missing artifact/marker, not generic "boot failed".

### `debugger`

- Diagnoses by last observed marker:
  - none: boot sector format, image, COM1 init.
  - `BOOT_OK` only: kernel load, GDT, entry, stack.
  - `KERNEL_INIT_OK` then panic: kernel subsystem after serial init.
- Suggests fixes but does not edit files.

### `safety-reviewer`

- Blocks commands using host disk passthrough, root QEMU, writeable host mounts, or uncontrolled bridge/TAP networking.
- Allows image-file QEMU tests as normal user.

### `critic`

- Tries to break the proposed solution before handoff.
- Checks edge cases: kernel size growth, boot sector overflow, stale marker semantics, missing segment init.
- Can request more tests but does not write code.

### `auditor`

- Verifies the pass is real: artifacts exist, logs are current, markers came from COM1, and no script hardcoded success.
- Checks artifact hashes, serial-log hash, QEMU status, and safety verdict from machine evidence.
- Runs after orchestrator believes the milestone is complete.
- Reports authenticity gaps separately from code quality gaps.

### `sentinel`

- Watches progress timestamps during long-running autonomous work.
- If progress is stale, asks orchestrator to stop, summarize state, and create a handoff.
- Does not kill processes unless a human-approved runtime policy allows it.

### `doc-maintainer`

- Updates `llms.txt` and memory docs when sections are added.
- Keeps required/optional marker language consistent.
- Does not invent platform-specific capabilities without source.

### `git-steward`

- Runs read-only Git preflight: status, branch, upstream, diff summary, tracked artifact checks.
- Prepares staging/commit/push plan only after explicit user request.
- Cannot stage, commit, push, rewrite history, reset, path checkout, clean, or force-push without explicit user request in the current turn.
- Flags broad staging, staged deletions, tracked build artifacts, and unknown same-file changes.

## Permission Rules

- Reviewer/debugger/tester/safety roles do not edit.
- Writer/doc-maintainer edit only assigned Markdown or source files.
- Git-steward inspects Git state by default; Git write actions require explicit user request.
- QEMU tests must use `build/os.img`, not `/dev/sdX`.
- Any destructive cleanup must be explicit and narrow.
- Stage explicit paths only; never stage deletions without confirmation.

## Concurrency Policy

- One writer owns a worktree/build directory at a time.
- One git-steward owns a staging/commit plan at a time.
- Test runner creates a build lock before QEMU tests and releases it after marker/evidence writing.
- QEMU test scripts write a pid file while running and record cleanup in evidence.
- Parallel review is allowed only for read-only roles.
- If multiple agents need implementation access, split by branch/worktree and merge through orchestrator.
- If multiple agents edit in parallel, publish through short-lived branches/PRs or a single orchestrated merge point.

## Research-Derived Notes

- Google Antigravity describes distinct orchestrator, explorer, worker, reviewer, critic, auditor, and sentinel-like patterns; this doc adapts those roles to a smaller OS harness.
- Oracle-style governance says reviewer output is not enough: keep traceable evidence for actions and side effects.
- Meta-style agent platforms favor shared tool interfaces plus role-specific skills instead of one huge agent prompt.
- Meta DRS-style risk awareness maps naturally to this harness: bootloader/linker/marker/safety changes need stronger review and validation than glossary/index edits.
- Google small-CL and Swift incremental-development guidance map to small, reviewable branches/commits with tests or validation evidence attached.
- Microsoft branch policy guidance maps to protected shared branches, PR validation, reviewers, and external status checks.
- Netflix PR-confidence guidance maps to stable test evidence and developer responsibility for deciding whether a change is safe to merge.
