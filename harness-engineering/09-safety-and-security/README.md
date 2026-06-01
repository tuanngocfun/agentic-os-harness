# 09 — Safety And Security

## Mục tiêu

QEMU is a normal userspace process when run normally, but isolation is not magic. Safety depends on command shape, user privileges, and what host resources are exposed.

## Safe Default

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
