# regression-validation

Use this skill before marking a harness or boot-path change complete.

## Inputs

- `AGENTS.md`
- `llms.txt`
- `03-os-harness-config/*.md`
- `06-validation/README.md`
- `09-safety-and-security/README.md`
- `.agent/skills/regression-validation/scripts/check_harness_contract.sh`

## Contract

- Critical stale patterns must not appear outside validation/reference anti-pattern text.
- Root `llms.txt` must exist and reference the core contracts.
- Executable skill files must exist for compile/run and regression validation.
- Marker semantics must remain required `BOOT_OK`, `KERNEL_INIT_OK`; optional `SHELL_READY`, `TESTS_PASS`; failure `BOOT_DISK_ERROR`, `KERNEL_PANIC`.
- Automated QEMU evidence must use dedicated serial files and exact marker parsing.

## Steps

1. Run `.agent/skills/regression-validation/scripts/check_harness_contract.sh`.
2. If source code exists, run `make all` and the `compile-and-run` skill.
3. Report every failing gate by file/pattern, not as a generic validation failure.
