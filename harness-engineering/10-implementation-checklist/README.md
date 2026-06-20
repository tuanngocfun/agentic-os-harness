# 10 — Implementation Checklist

## Phase 0 — Read Contracts

- [ ] Read root `AGENTS.md`.
- [ ] Read root `llms.txt`.
- [ ] Read `03-os-harness-config/build-commands.md`.
- [ ] Read `03-os-harness-config/boot-markers.md`.
- [ ] Read `06-validation/README.md`.
- [ ] Read `12-git-change-management/README.md` if this harness is inside a Git repo.
- [ ] Run `.agent/skills/git-change-management/scripts/git_preflight.sh` before editing tracked files.

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
- [x] Enforce the stage-2 LBA profile: 512-byte stage 1, stage 2 within 32 reserved sectors, kernel at LBA 33.

## Phase 2 — Verify Boot

- [ ] `make all` creates all required artifacts.
- [ ] `build/boot.bin` is exactly 512 bytes.
- [ ] `build/boot_config.inc` matches `kernel.bin` size.
- [ ] `make test` uses `-serial file:build/serial.log -monitor none -nic none`.
- [ ] QEMU status is captured; timeout `124` is the normal liveness pass, and early exit `0` fails unless this is an explicit shutdown test.
- [ ] Marker parser uses exact whole-line matching after CRLF normalization.
- [x] `make test` finds `STAGE2_OK`, `BOOT_OK`, `KERNEL_INIT_OK`, and `SHELL_READY`.
- [ ] No failure marker appears.
- [ ] Negative marker fixtures fail (`NOT_BOOT_OK`, `BOOT_OK_FAKE`, missing `KERNEL_INIT_OK`, panic after pass markers).
- [ ] High-risk changes have artifact, marker, drift, and safety evidence.

## Phase 3 — Add Features

- [ ] Add VGA after serial marker is stable.
- [ ] Add IDT without breaking `KERNEL_INIT_OK`.
- [ ] Add shell only after boot marker regression passes.
- [ ] Make `SHELL_READY` required only after shell exists.
- [ ] Add shell-runtime validation before claiming the shell works.
- [x] Process lifecycle, scheduler, syscall, user-mode, VFS, and ELF claims have targeted runtime gates.
- [ ] Add per-process descriptor ownership with fork, exec, and exit semantics.

## Phase 4 — Regression

- [ ] Run drift searches from `06-validation/README.md`.
- [ ] Run `make test` and confirm both boot-marker and shell-runtime phases pass.
- [x] Confirm generated `STAGE2_LOAD_SECTORS`, `KERNEL_LBA_START`, and `KERNEL_SECTORS` match built artifacts.
- [ ] Confirm risk classification matches changed files.
- [ ] Confirm machine evidence includes run id, timestamps, command status, artifact sizes/hashes, serial hash, marker verdict, and safety verdict.
- [ ] Update memory summaries without hand-editing machine evidence verdicts.
- [ ] Update `llms.txt` if docs or skills moved.
- [ ] Review QEMU safety commands.
- [ ] Confirm multi-agent concurrency rules: one writer owner per worktree, build lock, QEMU pid cleanup.
- [ ] Confirm Git preflight passes: status reported, diff summary captured, generated artifacts ignored/untracked, no unapproved staged deletions.

## Phase 5 — Git Handoff

- [ ] Report repo root, branch, upstream, and status.
- [ ] Report diff summary and changed files.
- [ ] Confirm no broad staging or unrequested Git write action happened.
- [ ] Include risk tier and rollback note.
- [ ] If user requested commit/push, show status and diff summary before staging explicit paths.

## Phase 6 — Handoff

- [ ] Summarize changed contracts.
- [ ] Include command evidence.
- [ ] Include marker verdict.
- [ ] List remaining risks and optional next steps.
