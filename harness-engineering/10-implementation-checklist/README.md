# 10 — Implementation Checklist

## Phase 0 — Read Contracts

- [ ] Read root `AGENTS.md`.
- [ ] Read root `llms.txt`.
- [ ] Read `03-os-harness-config/build-commands.md`.
- [ ] Read `03-os-harness-config/boot-markers.md`.
- [ ] Read `06-validation/README.md`.

## Phase 1 — Build Core

- [ ] Create `boot/boot.asm`.
- [ ] Create `kernel/entry.asm`.
- [ ] Create `kernel/kernel.c`.
- [ ] Create `kernel/serial.c` and `include/serial.h`.
- [ ] Create `kernel/string.c` and `include/string.h` with freestanding memory helpers.
- [ ] Create `linker.ld`.
- [ ] Create Makefile with `boot.bin`, `boot_config.inc`, `kernel.elf`, `kernel.bin`, `os.img`.
- [ ] Put `_start` in `.entry` and make `linker.ld` place `.entry` first.
- [ ] Link final `kernel.elf` through `i686-elf-gcc ... -lgcc` or document/test a strict no-runtime-helper policy.
- [ ] Use `-Iinclude -MMD -MP` for kernel C objects.
- [ ] Guard `BUILD_DIR` and `OS_IMG` before `dd` or `clean`.
- [ ] Initialize real-mode `DS`, `ES`, `SS`, and `SP` before bootloader memory access.
- [ ] Enforce phase-1 CHS `KERNEL_SECTORS <= 17`, or implement track-rolling CHS/LBA/2-stage loading.

## Phase 2 — Verify Boot

- [ ] `make all` creates all required artifacts.
- [ ] `build/boot.bin` is exactly 512 bytes.
- [ ] `build/boot_config.inc` matches `kernel.bin` size.
- [ ] `make test` uses `-serial file:build/serial.log -monitor none -nic none`.
- [ ] QEMU status is captured and non-`0`/non-`124` statuses fail immediately.
- [ ] Marker parser uses exact whole-line matching after CRLF normalization.
- [ ] `make test` finds `BOOT_OK`.
- [ ] `make test` finds `KERNEL_INIT_OK`.
- [ ] No failure marker appears.
- [ ] Negative marker fixtures fail (`NOT_BOOT_OK`, `BOOT_OK_FAKE`, missing `KERNEL_INIT_OK`, panic after pass markers).
- [ ] High-risk changes have artifact, marker, drift, and safety evidence.

## Phase 3 — Add Features

- [ ] Add VGA after serial marker is stable.
- [ ] Add IDT without breaking `KERNEL_INIT_OK`.
- [ ] Add shell only after boot marker regression passes.
- [ ] Make `SHELL_READY` required only after shell exists.

## Phase 4 — Regression

- [ ] Run drift searches from `06-validation/README.md`.
- [ ] Confirm no hardcoded stale `KERNEL_SECTORS`.
- [ ] Confirm risk classification matches changed files.
- [ ] Confirm machine evidence includes run id, timestamps, command status, artifact sizes/hashes, serial hash, marker verdict, and safety verdict.
- [ ] Update memory summaries without hand-editing machine evidence verdicts.
- [ ] Update `llms.txt` if docs or skills moved.
- [ ] Review QEMU safety commands.
- [ ] Confirm multi-agent concurrency rules: one writer owner per worktree, build lock, QEMU pid cleanup.

## Phase 5 — Handoff

- [ ] Summarize changed contracts.
- [ ] Include command evidence.
- [ ] Include marker verdict.
- [ ] List remaining risks and optional next steps.
