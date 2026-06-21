# 09 — Safety And Security

## Mục tiêu

QEMU is a normal userspace process when run normally, but isolation is not magic. Safety depends on command shape, user privileges, and what host resources are exposed.

The OS security posture is evidence-scoped. Functional gates prove specific boot, memory, syscall, scheduler, storage, and process behaviors; they do not prove broad adversarial hardening. The current posture is `known_red_team_attacks_blocked_security_not_complete`: known red-team probes are blocked by current blue controls, and new probes must keep raising the bar.

## Safe Default

All automated runtime gates require an explicit `QEMU_BIOS_DIR` and pass through `scripts/qemu_runtime.sh`. The helper pins QEMU 4.2.1 plus BIOS file sizes and SHA-256 hashes, truncates logs before launch, and records fresh runtime evidence. There is no auto-discovery, fallback BIOS directory, `SKIP_QEMU`, or stale-log pass.

```bash
qemu-system-i386 \
  -drive file=build/os.img,format=raw \
  -m 512M \
  -serial file:build/serial.log \
  -monitor none \
  -nic none \
  -display none \
  -no-reboot \
  > build/qemu.log 2>&1
```

This runs a guest OS from an image file, captures COM1 serial output into a dedicated evidence file, disables QEMU monitor input, and disables guest networking. `-serial mon:stdio` is allowed for named human debug mode only; it multiplexes COM1 with the monitor and should not be the automated evidence channel.

## Hard Rules

- Do not run QEMU as root for routine boot tests.
- Do not use `-drive file=/dev/sdX`.
- Do not allow `OS_IMG` or `BUILD_DIR` Makefile overrides to point outside `build/`.
- Do not passthrough physical disks, USB devices, PCI devices, or host block devices.
- Do not mount host folders writeable into the guest for early boot tests.
- Do not use uncontrolled bridge/TAP networking for basic boot verification.
- Do not claim "100% isolated" or "completely safe"; say safety depends on configuration.

## Approval Triggers

Require explicit human approval before:
- Running QEMU with host device passthrough.
- Using KVM acceleration in a shared/unknown environment.
- Adding writeable host mounts.
- Changing system networking for QEMU.
- Running commands with `sudo`.

## External Artifact Supply-Chain Policy

Prefer distribution package managers and existing pinned toolchains. When a required compiler, emulator firmware, script, archive, or binary must come directly from the Internet:

- Pin an exact release or immutable commit; never follow an unversioned “latest” URL.
- Fetch only over HTTPS from the upstream project or its documented release channel.
- Verify the upstream signature or published SHA-256 checksum when available.
- For unsigned raw artifacts, compare bytes or SHA-256 hashes from two independently hosted official mirrors before use.
- Never execute `curl | sh`, `wget | sh`, or an unreviewed installer with elevated privileges.
- Keep downloads in a temporary quarantine directory, inspect archive members and file types, and run them with least privilege and no host passthrough.
- Record source URLs, release tag, and hashes in the validation handoff.
- Use malware scanning when available, but treat it as supplementary evidence; a clean scan does not prove an artifact is trustworthy.

If provenance or integrity cannot be established, stop and request human approval instead of installing or executing the artifact.

## Failure Containment

- Use `timeout` in automated tests.
- Use `-no-reboot` to avoid hiding triple faults in reboot loops.
- Write COM1 serial logs to `build/serial.log` and QEMU diagnostics to `build/qemu.log`.
- Treat timeout status `124` as the normal acceptable status for boot/shell liveness after marker parsing succeeds.
- Treat early QEMU exit status `0` as failure by default; it can indicate shutdown or a triple fault after markers. Allow it only for an explicit shutdown test.
- Treat `KERNEL_PANIC` and `BOOT_DISK_ERROR` as hard failures.

## Security Review Checklist

- QEMU command uses only `build/os.img`.
- Automated QEMU command includes `-monitor none` and `-nic none`.
- Automated serial evidence uses `-serial file:build/serial.log`.
- No host disk path appears in scripts.
- Makefile guards reject unsafe image/build paths before `dd` or `clean`.
- No root requirement in normal workflow.
- No hidden mutation outside `build/`.
- Docs clearly separate toy/teaching OS from production OS.

## Adversarial Findings Gate

`make test-red-team` is the current guest-only adversarial gate. `make test-blue-team` is an alias for the same runtime defense proof. A pass means the known attack probes were attempted, blocked, and written to `build/red-team/findings.jsonl`; it is not a broad hardening pass.

Current blocked findings:
- `RT-HARNESS-001`: no-capability ring-3 marker forgery is rejected.
- `RT-HARNESS-003`: retired-token, namespace-crossing, and marker replay attacks are rejected.
- `RT-SYSCALL-001`: ring-3 access to test-only ABI marker syscall is rejected.
- `RT-EXEC-001`: exec residual heap mapping is rejected by post-exec pointer validation.
- `RT-SCHED-001`: preemptive interrupt-frame tasks cannot enter cooperative `yield()` switching.
- `RT-FS-001`: SimpleFS truncate/write sector exhaustion is blocked through sector reuse.
- `RT-FS-002`: relative, nested, `.`, and `..` filesystem names are rejected.
- `RT-EXEC-002`: writable VFS descriptors do not survive successful exec.
- `RT-ELF-001`: overlapping ELF load page ranges are rejected before partial user mappings are created.
- `RT-EXEC-003`: failed exec attempts do not leak address-space frames.
- `RT-PROC-001`: destroying a process with a private address space reclaims owned frames.
- `RT-PROC-002`: failed fork cloning under low-frame exhaustion rolls back child resources and accounting.

Patch planning and next hardening work live in `harness-engineering/blue-team/PATCH_PLAYBOOKS.md`.
