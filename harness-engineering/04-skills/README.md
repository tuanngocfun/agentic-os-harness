# 04 — Skills cho OS Harness

## Mục tiêu

Skills là capability on-demand. Agent chỉ load skill khi intent match, tránh nhồi toàn bộ OS workflow vào context ngay từ đầu.

Mỗi skill phải có:
- Trigger rõ ràng: "Use this when..."
- Inputs/outputs cụ thể
- Constraints chống lỗi nguy hiểm
- Verification steps có exit code hoặc marker verdict

## Skill Specs

### 1. `write-bootloader`

**Trigger:** Use when user asks to create or modify the x86 boot sector.

**Inputs:** `boot/boot.asm`, kernel load address, kernel sector count.

**Output:** Flat `build/boot.bin`, exactly boot-sector compatible, ending with `0xAA55`.

**Instructions:**
1. Build boot sector with `nasm -f bin boot/boot.asm -o build/boot.bin`.
2. Keep bootloader separate from kernel objects.
3. Initialize `DS`, `ES`, `SS`, and `SP` before memory/string/disk access.
4. Include generated `boot_config.inc` for `KERNEL_SECTORS`; do not hardcode stale counts.
5. Keep sector 0 minimal, load `stage2.bin`, and let stage 2 load the kernel from LBA 33.
6. Emit `STAGE2_OK` from stage 2 and `BOOT_OK` after protected-mode transition through COM1 serial, not VGA-only output. Treat them as loader-progress evidence, not kernel identity proof unless a magic/header check exists.
7. Emit `BOOT_DISK_ERROR` through COM1 before halting on disk read failure.

**Verification:**
- `make all`
- Confirm no Makefile path writes `boot.o` to sector 0.
- Confirm `build/boot.bin` is exactly 512 bytes.
- Confirm `build/stage2.bin` fits within the reserved 32-sector area.
- Confirm `build/boot_config.inc` records `STAGE2_LOAD_SECTORS`, `KERNEL_LBA_START`, and `KERNEL_SECTORS`.
- `make test` must find `STAGE2_OK` and `BOOT_OK`.

### 2. `setup-gdt`

**Trigger:** Use when protected mode, segment selectors, or triple faults are involved.

**Inputs:** `boot/boot.asm`, GDT layout, selector constants.

**Output:** Valid code/data descriptors and far jump into protected mode.

**Instructions:**
1. Keep code selector and data selector consistent with GDT order.
2. Load `ds`, `ss`, `es`, `fs`, and `gs` after the far jump.
3. Set stack before calling kernel code.

**Verification:**
- `make test` must progress from `BOOT_OK` to `KERNEL_INIT_OK`.
- If QEMU exits/reboots immediately, inspect protected-mode transition first.

### 3. `kernel-entry`

**Trigger:** Use when adding or changing `kernel/entry.asm` or `kernel_main()`.

**Inputs:** `kernel/entry.asm`, `kernel/kernel.c`, `linker.ld`.

**Output:** `_start` calls `kernel_main()` at the address expected by bootloader.

**Instructions:**
1. Put `_start` in `section .entry`.
2. Assemble `entry.asm` with `nasm -f elf32`.
3. Link entry and kernel C objects into `kernel.elf` with `.entry` placed first by `linker.ld`.
4. Convert `kernel.elf` to `kernel.bin` with `i686-elf-objcopy -O binary`.
5. Regenerate boot sector config after `kernel.bin` size changes.

**Verification:**
- `make all`
- `make test` must find `KERNEL_INIT_OK`.

### 4. `serial-driver`

**Trigger:** Use when automated boot tests, COM1 output, or marker capture fails.

**Inputs:** `kernel/serial.c`, `include/serial.h`, bootloader serial routines.

**Output:** COM1 serial driver usable by bootloader and kernel markers.

**Instructions:**
1. Use COM1 base port `0x3F8`.
2. Initialize serial before printing any required marker.
3. Keep marker names exact and newline-terminated.

**Verification:**
- `make run-serial` should show marker text on stdout.
- `make test` should write markers to `build/serial.log`.

### 5. `compile-and-run`

**Trigger:** Use when the user asks to build, run, test, or validate the OS.

**Inputs:** Makefile, `scripts/boot_test.sh`, all boot/kernel source files.

**Output:** Build verdict, boot-marker verdict, and current runtime verdicts.

**Instructions:**
1. Run `make all`.
2. Run `make test`; in the current shell phase this runs both `scripts/boot_test.sh` and `scripts/shell_test.sh`.
3. Report missing artifacts or missing markers directly.
4. Do not mask failure by claiming success from partial output.
5. Treat timeout `124` as the normal pass status for a live kernel/shell after exact marker parsing passes. Treat early QEMU exit status `0` as failure unless a named shutdown test explicitly sets an allow-exit mode.
6. Use dedicated serial evidence (`-serial file:build/serial.log -monitor none -nic none`), not `mon:stdio`.

**Verification:**
- Required boot artifacts: `build/boot.bin`, `build/boot_config.inc`, `build/kernel.elf`, `build/kernel.bin`, `build/os.img`, `build/serial.log`, `build/qemu.log`.
- Required shell-runtime artifacts after `make test`: `build/serial.shell.log`, `build/vga.shell.bin`, `build/vga.shell.txt`, `build/qemu.shell.log`, `build/qemu.shell.monitor.log`.
- Required markers: `STAGE2_OK`, `BOOT_OK`, `KERNEL_INIT_OK`, `SHELL_READY`.
- Marker parser uses exact whole-line matching after CRLF normalization.
- Early QEMU exit after markers fails by default; it can indicate shutdown or triple fault.
- `build/serial.log` and evidence records come from the current run.
- Shell runtime must show `Available commands:` in decoded VGA text. Default `make test` still proves only `help`; use `make test-shell-io` for the separate `echo ok` proof.

### 6. `debug-boot-failure`

**Trigger:** Use when QEMU hangs, triple faults, or marker checks fail.

**Inputs:** `build/serial.log`, Makefile, bootloader, linker script.

**Output:** Root cause hypothesis with next concrete fix.

**Instructions:**
1. Classify failure by last marker seen.
2. If no marker appears, check boot sector format and COM1 init.
3. If `STAGE2_OK` appears but `BOOT_OK` does not, check stage-2 LBA reads, GDT, stack, and protected-mode entry.
4. If `KERNEL_PANIC` appears, read panic context before changing code.
5. If a partial load is suspected, verify generated `KERNEL_LBA_START=33`, `KERNEL_SECTORS`, stage-2 bounds, and BIOS extended-read status.

**Verification:**
- Proposed fix must name the file and invariant it restores.

### 7. `shell-bringup`

**Trigger:** Use when implementing first interactive shell or command loop.

**Inputs:** keyboard/input driver status, serial driver, VGA driver.

**Output:** Shell loop that can emit `SHELL_READY` and pass a VGA-backed command test.

**Instructions:**
1. Do not make `SHELL_READY` required until shell exists.
2. Emit `SHELL_READY` only after `shell_init()` has completed.
3. Keep shell bringup behind existing boot markers.
4. Preserve `KERNEL_INIT_OK` regression.
5. Add runtime evidence for keyboard input, command dispatch, and VGA output; serial markers alone are not enough for shell truthfulness.

**Verification:**
- `make test` still passes required markers.
- `scripts/shell_test.sh` must render `help` output in decoded VGA text.
- Do not claim timer commands, memory management, scheduler, syscall, or user mode as complete unless each has its own runtime test.

### 8. `regression-validation`

**Trigger:** Use before marking a boot/harness change complete.

**Inputs:** changed files, serial log, previous marker expectations.

**Output:** Pass/fail report for build, boot, drift, and safety gates.

**Instructions:**
1. Run consistency searches for stale bad patterns.
2. Confirm artifact names match the public contract.
3. Confirm QEMU command uses image file, not host disk.
4. Confirm first-stage bootloader size and generated sector count gates are present.
5. Run negative marker-parser fixtures: fake marker substrings must not pass.
6. Confirm Makefile path guards run before `dd` and `clean`.

**Verification:**
- No stale critical patterns remain outside anti-pattern/reference docs.
- Required markers and failure markers are documented consistently.

### 9. `kernel-runtime-primitives`

**Trigger:** Use when C kernel code adds struct copies, memory initialization, or link errors mention `memcpy`, `memset`, `memmove`, or `memcmp`.

**Inputs:** `kernel/string.c`, `include/string.h`, Makefile object list.

**Output:** Freestanding memory helper functions linked into `kernel.elf`.

**Instructions:**
1. Implement small freestanding `memcpy`, `memset`, `memmove`, and `memcmp`.
2. Do not include libc headers or call host libc.
3. Add `kernel/string.c` to `KERNEL_OBJECTS`.
4. Keep prototypes in `include/string.h`.

**Verification:**
- `make all` links without unresolved libc/runtime memory symbols.
- `make test` still passes required markers.

### 10. `git-change-management`

**Trigger:** Use when the task involves repo state, branch/worktree planning, staging, committing, pushing, or final handoff.

**Inputs:** Git repo root, `.gitignore`, changed files, validation evidence, `12-git-change-management/README.md`.

**Output:** Git preflight verdict and safe staging/handoff plan.

**Instructions:**
1. Run `git status --short --branch` before editing and before handoff.
2. Run `.agent/skills/git-change-management/scripts/git_preflight.sh`.
3. Classify the change risk and name the validation gates required by that risk.
4. Do not stage, commit, push, rewrite history, or clean files unless the user explicitly asks in the current turn.
5. If staging is requested, stage explicit file paths only.
6. Never stage deletions without explicit confirmation.
7. Confirm generated artifacts are ignored and untracked.

**Verification:**
- Git preflight passes.
- Handoff includes repo root, branch, status, diff summary, risk tier, and validation evidence.
- No write Git action happened unless requested.

### 11. `agent-routing-and-risk`

**Trigger:** Use before MiMo v2.5pro or any agent starts advanced OS core work, broad feature work, or progress reporting.

**Inputs:** `harness_profile.yaml`, `13-agent-routing-and-risk/README.md`, changed files, intended subsystem claim.

**Output:** Route selection, allowed path set, required gates, and claim-status verdict.

**Instructions:**
1. Pick exactly one route from `harness_profile.yaml`.
2. Keep the diff inside that route unless the handoff explicitly explains why a second route is necessary.
3. Never upgrade a subsystem from scaffold/partial to working without a targeted runtime gate.
4. Prefer the current P0: waitpid options, minimal signal delivery, and pipe ownership on the proven VM layer.
5. Do not add networking, graphics, or more shell breadth before waitpid, signals, and pipes have targeted lifecycle gates.

**Verification:**
- `check_harness_contract.sh` passes.
- Handoff names route, claim, evidence, remaining unproven behavior, and rollback path.
- Progress percentage distinguishes boot-to-shell from credible protected OS core.

## Skill Authoring Rules

- Keep `AGENTS.md` as the menu; move detailed workflow here.
- Each skill must stop on failed verification.
- Skills must report evidence, not vibes: command, exit status, marker verdict, and touched files.
- Executable project-scoped skills live under `.agent/skills/<skill-name>/SKILL.md`; this README is the human index.
- Never make a later phase depend on optional markers unless that feature exists.
- Keep skills as a compass, not an encyclopedia: name the key files, exact commands, and risk gates.
- Validate trajectory, not only final text: wrong tool order is a failure even if the prose sounds right.
