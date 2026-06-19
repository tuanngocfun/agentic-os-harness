# Blue Team Harness

This area turns red-team findings into defenses and strict regression gates. It does not mark a finding blocked until a patch lands and a verification gate proves the old attack no longer succeeds.

## Current Mode

- Security posture: known red-team attacks blocked; broader hardening remains unclaimed.
- Functional gates remain `make test` and `make test-deep`.
- Adversarial gates are `make test-red-team` and the alias `make test-blue-team`.
- Defense evidence is tracked in `build/red-team/findings.jsonl`.

## Patch Discipline

- Patch one finding at a time.
- Keep the original red reproduction or an equivalent attack probe after the blue verification gate proves it is blocked.
- Update `CURRENT_FINDINGS.md`, `PATCH_PLAYBOOKS.md`, `harness_profile.yaml`, and the contract checker together.
- Add new attacks after every blocked finding so the loop keeps raising the bar instead of stopping at one checklist.
