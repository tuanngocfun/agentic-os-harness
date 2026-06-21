# Blue Team Harness

This area turns red-team findings into defenses and strict regression gates. It does not mark a finding blocked until a patch lands and a verification gate proves the old attack no longer succeeds.

## Current Mode

- Security posture: known red-team attacks blocked; broader hardening remains unclaimed.
- Functional gates remain `make test` and `make test-deep`.
- Adversarial gates are `make test-red-team` and the alias `make test-blue-team`; both require an explicit validated `QEMU_BIOS_DIR`.
- Defense evidence is tracked in `build/red-team/findings.jsonl`; QEMU provenance is tracked separately in `build/qemu-runtime.jsonl`.

## Patch Discipline

- Patch one finding at a time.
- Keep the original red reproduction or an equivalent attack probe after the blue verification gate proves it is blocked.
- Update `CURRENT_FINDINGS.md`, `PATCH_PLAYBOOKS.md`, `harness_profile.yaml`, and the contract checker together.
- Add new attacks after every blocked finding so the loop keeps raising the bar instead of stopping at one checklist.
## Meta-Loop Gates

- `make test-static-analysis` verifies strict cross-compiler syntax, all selftest branches, cppcheck, shell syntax, editor JSON, and header provenance.
- `make test-red-team-tooling` reproduces known tooling/harness weaknesses and verifies their controls.
- `make test-meta-loop` composes tooling red-team, guest red-team, and contract validation.

The workflow is defined in `../14-meta-loop/README.md`.
## VM Control Gate

`make test-vm` proves the scoped COW, lazy heap, guard-page, and rollback controls. `make test-red-team` separately attacks those controls through `RT-VM-001..004`. Neither gate is a broad memory-safety or production-security certification.
