# Red Team Harness

This area is for adversarial probes against the teaching OS. A passing red-team gate means the known attack probes were attempted, blocked by the current blue controls, and recorded. It is not a broad security signal.

## Scope

- Guest-only probes inside QEMU.
- Automated QEMU runs use `-monitor none`, `-nic none`, no host passthrough, and `build/os.img` only.
- Findings and defense status are written as JSONL evidence by scripts, not by hand-edited status text.
- Kernel behavior may be patched by blue-team work, but each patch must keep the red probe so regressions are caught.

## Current Gate

```bash
make test-red-team
```

Expected evidence:
- `RED_TEAM_TEST`
- `RED_MARKER_FORGERY_BLOCKED`
- `RED_SYSCALL_PRIVILEGE_BLOCKED`
- `RED_EXEC_RESIDUAL_MAPPING_BLOCKED`
- `RED_SCHED_YIELD_MIXING_BLOCKED`
- `RED_SIMPLEFS_DOS_BLOCKED`
- `RED_VFS_NAMESPACE_BLOCKED`
- `RED_ELF_OVERLAP_BLOCKED`
- `RED_EXEC_FAILURE_CLEANUP_BLOCKED`
- `RED_PROCESS_DESTROY_CLEANUP_BLOCKED`
- `RED_FORK_FAILURE_CLEANUP_BLOCKED`
- `RED_EXEC_FD_LEAK_BLOCKED`
- `RED_DEFENSES_OK`
- `build/red-team/findings.jsonl`

Failure marker:
- `RED_TEAM_FAIL`

## Rules For New Probes

- Add one catalog entry before adding a runtime probe.
- Keep probes deterministic enough for CI-style parsing.
- Do not use host networking, host device passthrough, host mounts, or QEMU monitor access.
- Document impact, attack gate, expected marker, blue control, and follow-up hardening gate.
