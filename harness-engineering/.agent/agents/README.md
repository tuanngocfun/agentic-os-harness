# Agent Role Materialization

This directory records the role contracts that must be turned into platform-specific agent definitions or wrappers before claiming permission boundaries are enforced.

## Required Roles

- `orchestrator`: may coordinate, but cannot mark complete without machine evidence.
- `code-writer`: may edit assigned source/docs only.
- `code-reviewer`: read-only findings.
- `test-runner`: may run build/test scripts only.
- `debugger`: read-only log diagnosis.
- `safety-reviewer`: read-only QEMU/host safety review.
- `auditor`: validates evidence authenticity and stale-log resistance.
- `git-steward`: read-only Git status/diff/artifact preflight; prepares staging plans only after explicit user request.

## Enforcement Gate

Before real implementation, add platform-specific definitions such as `.codex/agents/*.toml`, `.claude/agents/*.md`, or wrapper scripts, then extend `.agent/skills/regression-validation/scripts/check_harness_contract.sh` to verify they exist.

Git role enforcement must also verify that staging/commit/push wrappers require explicit file paths and explicit user approval for deletions or history/remote mutations.
