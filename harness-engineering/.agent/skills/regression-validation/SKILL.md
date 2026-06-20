# regression-validation

Use this skill before marking a harness or boot-path change complete.

## Inputs

- `AGENTS.md`
- `llms.txt`
- `03-os-harness-config/*.md`
- `06-validation/README.md`
- `09-safety-and-security/README.md`
- `12-git-change-management/README.md`
- `.agent/skills/regression-validation/scripts/check_harness_contract.sh`
- `.agent/skills/git-change-management/scripts/git_preflight.sh`

## Contract

- Critical stale patterns must not appear outside validation/reference anti-pattern text.
- Root `llms.txt` must exist and reference the core contracts.
- Executable skill files must exist for compile/run and regression validation.
- Executable skill files must exist for Git change management.
- Marker semantics must remain required `STAGE2_OK`, `BOOT_OK`, `KERNEL_INIT_OK`, `SHELL_READY`; optional `TESTS_PASS`; failure `BOOT_DISK_ERROR`, `KERNEL_PANIC`.
- Automated QEMU evidence must use dedicated serial files and exact marker parsing.
- Git workflow must require status/diff evidence before handoff and explicit user request before stage/commit/push.

## Steps

1. Run `.agent/skills/regression-validation/scripts/check_harness_contract.sh`.
2. Run `.agent/skills/git-change-management/scripts/git_preflight.sh` when inside a Git repo.
3. If source code exists, run `make all` and the `compile-and-run` skill.
4. Report every failing gate by file/pattern, not as a generic validation failure.
