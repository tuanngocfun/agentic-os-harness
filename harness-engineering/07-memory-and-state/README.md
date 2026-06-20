# 07 — Memory And State

## Mục tiêu

Memory files preserve agent state across sessions. They should capture decisions and evidence, not just prose summaries.

## Recommended Files

```
agent_docs/
├── progress.md
├── architecture.md
├── running_tests.md
├── known_issues.md
├── decisions.md
└── evidence-log.md
```

## `progress.md` Template

```markdown
# Progress

## Current Goal
<one sentence>

## Status
IN_PROGRESS | BLOCKED | COMPLETE

## Completed
- [x] <step> — <date/time> — evidence: <command/log/marker>

## Next
- [ ] <step>

## Blockers
- <blocker or None>
```

## `architecture.md` Template

```markdown
# Architecture

## Boot Contract
- `boot.bin`: flat 512-byte sector at image sector 0
- `kernel.elf`: linked ELF at 0x1000
- `stage2.bin`: LBA-capable loader copied into reserved sectors 1..32
- `kernel.bin`: raw kernel copied starting at LBA 33

## Marker Contract
- Required: STAGE2_OK, BOOT_OK, KERNEL_INIT_OK, SHELL_READY
- Optional: TESTS_PASS
- Failure: BOOT_DISK_ERROR, KERNEL_PANIC

## Memory Map
<table>
```

## `known_issues.md` Template

```markdown
# Known Issues

## Critical
- Symptom:
  Cause:
  Fix:
  Evidence:

## Warning
- Symptom:
  Cause:
  Fix:
```

## `decisions.md` Template

```markdown
# Decisions

## YYYY-MM-DD — <Decision>

Decision:
Reason:
Alternatives considered:
Compatibility impact:
```

## `evidence-log.md` Template

Human summaries may reference evidence, but machine verdicts should be append-only JSONL written by scripts. Do not let writer agents hand-edit pass/fail results.

```jsonl
{"run_id":"<timestamp>-<short-random>","task":"<task>","git":{"repo_root":"<path>","branch":"<branch>","commit":"<hash-or-none>","status_short":"clean|dirty","diff_stat":"<summary>","tracked_build_artifacts":false,"staged_paths":[],"deleted_paths_staged":[]},"started_at":"<iso8601>","ended_at":"<iso8601>","commands":[{"cmd":"make all","status":0},{"cmd":"make test","status":0}],"artifacts":[{"path":"build/boot.bin","bytes":512,"sha256":"<hash>"},{"path":"build/stage2.bin","bytes":"<bytes>","sha256":"<hash>"},{"path":"build/kernel.bin","bytes":"<bytes>","sha256":"<hash>"}],"qemu_status":124,"serial_log_sha256":"<hash>","markers":{"STAGE2_OK":true,"BOOT_OK":true,"KERNEL_INIT_OK":true,"SHELL_READY":true,"BOOT_DISK_ERROR":false,"KERNEL_PANIC":false},"image_layout":{"stage2_reserved_sectors":32,"kernel_lba_start":33,"kernel_sectors":"<count>","ok":true},"safety":{"root_qemu":false,"host_disk_passthrough":false,"monitor_none":true,"nic_none":true},"risk":"high","verdict":"pass"}
```

Recommended files:
- `agent_docs/evidence-log.md` — human-readable summaries.
- `build/evidence.jsonl` — machine-written run records.
- `build/serial.log` — COM1 output from current run.
- `build/qemu.log` — emulator diagnostics.
- Git status/diff output in handoff summaries; do not hand-edit machine pass/fail verdicts.

## Update Rules

- Update `progress.md` after meaningful phases.
- Update `decisions.md` when changing artifact, marker, memory, or safety contracts.
- Update `decisions.md` when changing Git workflow, staging, branch, or handoff contracts.
- Append machine evidence after autonomous build/test/fix loops; summarize it in `evidence-log.md`.
- Do not delete completed history; append corrections with date/context.
- Treat stale or hand-written pass claims as untrusted until current scripts regenerate evidence.

## Self-Succession / Long-Run Handoff

For long autonomous runs, create a handoff entry before context gets too large:

```markdown
## Handoff

Current milestone:
State files updated:
Last passing command:
Last marker verdict:
Git status/diff:
Open risks:
Next exact action:
```

This makes progress resumable from files instead of relying on conversation memory.

## Freshness Checks

- Validate referenced paths still exist.
- Re-run Git preflight after branch changes or before handoff.
- Re-run drift searches after large doc changes.
- Mark stale context explicitly instead of letting agents trust old guidance.
