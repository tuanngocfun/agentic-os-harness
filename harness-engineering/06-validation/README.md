# 06 — Validation

## Mục tiêu

Validation biến harness từ docs thành feedback loop. Một run chỉ pass khi Git state rõ ràng, build artifacts đúng, serial markers đúng, regression không bị phá, và QEMU safety được giữ.

## Git Preflight

Run this before editing tracked files and before final handoff:

```bash
.agent/skills/git-change-management/scripts/git_preflight.sh
```

Pass condition:
1. Repo root resolves correctly.
2. `git status --short --branch` is reported.
3. Diff summary is reported.
4. No generated build artifact is tracked.
5. `.gitignore` protects `build/` and build outputs.
6. No staged deletion exists unless explicitly confirmed by the user.
7. No stage/commit/push occurred unless explicitly requested.

## Boot-Test Protocol

Required artifacts:
- `build/boot.bin`
- `build/stage2.bin`
- `build/boot_config.inc`
- `build/kernel.elf`
- `build/kernel.bin`
- `build/os.img`
- `build/serial.log` after test
- `build/qemu.log` after test
- `build/serial.shell.log` after shell-runtime test
- `build/vga.shell.txt` after shell-runtime test

Required markers:
- `STAGE2_OK`
- `BOOT_OK`
- `KERNEL_INIT_OK`

Shell-phase marker:
- `SHELL_READY`

`SHELL_READY` is optional before a shell exists. Once `shell_init()`/`shell_run()` are implemented and the project `AGENTS.md` promotes shell bringup, `make test` must require `SHELL_READY`.

Optional markers:
- `TESTS_PASS`

Failure markers:
- `BOOT_DISK_ERROR`
- `KERNEL_PANIC`

Pass condition:
1. `make all` exits 0.
2. `make test` exits 0.
3. `build/serial.log` contains all required markers for the current phase.
4. `build/serial.log` does not contain failure markers.
5. `build/boot.bin` is exactly 512 bytes.
6. `build/stage2.bin` fits within the reserved 32-sector stage-2 area.
7. `build/boot_config.inc` records `STAGE2_LOAD_SECTORS`, `KERNEL_LBA_START`, and the generated kernel sector count.
8. The kernel starts at LBA 33 and the total image stays within the 2880-sector floppy image.
9. `build/serial.log` was created/truncated by the current run, not reused from a stale pass.
10. For live kernel/shell tests, QEMU reaches timeout `124`; early exit `0` fails unless this is an explicit shutdown test.
11. Shell-runtime test decodes VGA text and finds the visible shell banner plus `Available commands:`.

## Shell-Runtime Protocol

Current project phase:
- `make test` runs `scripts/boot_test.sh`, then `scripts/shell_test.sh`.
- `scripts/shell_test.sh` starts QEMU with a stdio monitor, sends keyboard events, dumps VGA text memory, and parses `build/vga.shell.txt`.
- This proves keyboard IRQ input, shell dispatch, and VGA text output for `help`.
- It does not prove timer accuracy, memory allocation, scheduler context switching, syscall dispatch from user space, ring-3 isolation, or argument-bearing shell commands. Those require targeted deep gates.

Do not promote a subsystem from "scaffold" to "working" until it has a targeted runtime gate and evidence artifact.

## Default Gates vs Deep Gates

Default gates must stay fast, stable, and route-local:
- `make all`
- `make test`
- `check_harness_contract.sh`
- `git_preflight.sh`
- `git diff --check`

Deep gates are explicit and may rebuild with selftest defines:
- `make test-deep`
- `make test-syscall`
- `make test-exception`
- `make test-exception-div0`
- `make test-exception-gpf`
- `make test-exception-pagefault`
- `make test-scheduler`
- `make test-paging`
- `make test-memory`
- `make test-usermode`
- `make test-timer`
- `make test-timer-preemption`
- `make test-allocator`
- `make test-address-space`
- `make test-syscall-negative`
- `make test-syscall-file`
- `make test-elf-loader`
- `make test-process-syscall`
- `make test-process-lifecycle`
- `make test-e820-frame`
- `make test-ramdisk`
- `make test-vfs`
- `make test-scheduler-safety`
- `make test-shell-io`

Adversarial gates are explicit red/blue defense-regression probes:
- `make test-red-team`
- `make test-blue-team`

Rules:
- Do not put deep probes directly in the default `kernel_main()` path.
- Do not let a deep probe run inside `make test` unless the project phase explicitly promotes it.
- A deep gate fails unless it triggers and observes the intended behavior.
- A red/blue gate passes only when known attack probes are attempted, blocked, and recorded; it is not a broad hardening pass.
- After a deep gate, rebuild or restore the default image so the next boot test is not contaminated by selftest defines.

## Harness Profile Contract

`harness_profile.yaml` is the current source of truth for:
- boot loader profile and sector limit;
- current required, optional, and failure markers;
- claim status for each subsystem;
- fast gates and deep gates;
- security posture and adversarial gates;
- format policy for Markdown/YAML/JSONL/HTML artifacts;
- MiMo next-task priority.

Any doc, skill, or handoff that conflicts with the profile is stale. Fix the conflict before accepting implementation work.

Current claim policy:
- `bootloader`, `protected_mode_entry`, `serial_markers`, `keyboard_irq`, and `shell_help` are claimable with current default gates.
- `syscall` and `exception_panic` are claimable only through their targeted deep gates.
- `timer_ticks` is claimable only through `make test-timer`.
- `paging` is claimable for map/unmap, permission-bit bookkeeping, CR0.WP-backed supervisor write-fault evidence, unmap+invlpg fault evidence, user/supervisor enforcement through the ring-3 page-fault gate, and per-process CR3 isolation through `make test-address-space`. Do not claim complete memory protection yet.
- `syscall` is claimable for the ABI through `make test-syscall`, for ring-3 negative paths through `make test-syscall-negative`, and for VFS-backed ring-3 file I/O through `make test-syscall-file`.
- `scheduler` is claimable as ready-queue rotation, explicit cooperative context execution through `make test-scheduler`, IRQ0 timer preemption through `make test-timer-preemption`, and priority/fairness/critical-section evidence through `make test-scheduler-safety`.
- `memory_info` is claimable as E820-backed usable-memory detection through `make test-memory` and `make test-e820-frame`.
- `frame_allocator` is claimable for allocation, free accounting, reuse, and low-frame exhaustion through `make test-e820-frame`.
- `allocator` is claimable only as fixed-heap `kmalloc`/`kfree` allocation, reuse, free/coalescing accounting, and exhaustion through `make test-allocator`; it is not frame free/reuse accounting.
- `block_device` is claimable only as a reserved, mapped ramdisk block device through `make test-ramdisk`.
- `filesystem` is claimable only as the kernel VFS + flat SimpleFS runtime gate through `make test-vfs`; this does not claim file syscalls, ELF loading, persistence, directories, delete/rename, or POSIX semantics.
- `elf_loader` is claimable as ELF32/i386 loading through `make test-elf-loader` and the VFS-backed exec routes: header/program-header validation, PT_LOAD materialization, BSS zero-fill, rejection paths, and transfer into a ring-3 entry. This does not claim argv/envp, dynamic linking, or persistent executable storage.
- `process` is claimable for process-record setup, per-process CR3 switching, syscall/brk/exec entry, true fork parent/child return, blocking wait, exit-to-scheduler, zombie reap, copied-address-space isolation, and exec image replacement through `make test-usermode`, `make test-address-space`, `make test-process-syscall`, and `make test-process-lifecycle`.
- `user_mode` is claimable only as a ring-3 transition and user/supervisor page-fault proof through `make test-usermode`.
- Default `scripts/shell_test.sh` must stay scoped to shell readiness plus `help` command rendering. `scripts/shell_io_test.sh` is the separate targeted route for `echo ok`; argument-bearing commands beyond that need their own unambiguous I/O proof.
- Copy-on-write fork, `waitpid` options, argv/envp, signals, per-process descriptor tables, persistent storage, networking, graphics mode, and additional shell breadth remain unclaimed until they have targeted runtime gates.
- Security posture is `known_red_team_attacks_blocked_security_not_complete`; `make test-red-team` currently proves the known attack probes are blocked and writes JSONL evidence.

## Build-Config Rebuild Protocol

`KERNEL_DEFINES` changes are build inputs. They are not source files, so plain Makefile dependency tracking will not notice a manual transition such as:

```bash
make -B all KERNEL_DEFINES=-DENABLE_PAGING_SELFTEST
make all
```

The Makefile must keep a `build/kernel_defines.stamp` file, make C objects depend on it, and update the stamp only when `KERNEL_DEFINES` content changes. This makes a default `make all` rebuild kernel objects after a selftest image was built, without relying on every agent to remember `-B`.

Validation:
1. Build a selftest image with a selftest define.
2. Run plain `make all`.
3. Confirm C objects rebuild without the selftest define.
4. Run `make test` and confirm normal boot has no selftest markers such as `PAGING_TEST`.

## Format Contract

Use the format split from `considerations/`:
- Markdown: agent instructions, role docs, skills, checklists, and human-readable guides.
- YAML: single-source profile/config such as `harness_profile.yaml`.
- JSONL: append-only runtime evidence written by scripts, not by prose editors.
- HTML: rendered reports and dashboards only; do not use HTML as the primary agent instruction contract.

## Marker Parser

Minimal shell parser:

```bash
required="STAGE2_OK BOOT_OK KERNEL_INIT_OK"
failures="BOOT_DISK_ERROR KERNEL_PANIC"
log="build/serial.log"
clean="build/serial.clean.log"

pass=true
tr -d '\r' < "$log" > "$clean"

for marker in $required; do
  if ! grep -Fxq "$marker" "$clean"; then
    echo "[FAIL] missing $marker"
    pass=false
  fi
done

for marker in $failures; do
  if grep -Fxq "$marker" "$clean"; then
    echo "[FAIL] found $marker"
    pass=false
  fi
done

[ "$pass" = true ]
```

## Drift Checks

Run these searches before claiming docs are implementation-ready. Expected result: no matches outside validation/reference material.

```bash
rg -n 'nasm -f elf32[[:space:]]+boot/boot[.]asm' harness-engineering --glob '!**/06-validation/**' --glob '!**/11-reference/**'
rg -n 'dd if=build/boot[.]o' harness-engineering --glob '!**/06-validation/**' --glob '!**/11-reference/**'
rg -n 'OBJECTS[[:space:]]*=.*BOOT' harness-engineering --glob '!**/06-validation/**' --glob '!**/11-reference/**'
rg -n 'BIOS[[:space:]]+INT[[:space:]]+10h.*serial|serial.*BIOS[[:space:]]+INT[[:space:]]+10h|INT[[:space:]]+10h.*serial-captured|serial-captured.*INT[[:space:]]+10h' harness-engineering --glob '!**/06-validation/**' --glob '!**/11-reference/**'
rg -n 'an toàn 100[%]|isolated[[:space:]]+completely' harness-engineering --glob '!**/06-validation/**' --glob '!**/11-reference/**'
rg -n 'KERNEL_SECTORS equ (15|[0-9]+)' harness-engineering --glob '!**/06-validation/**' --glob '!**/11-reference/**' --glob '!**/03-os-harness-config/build-commands.md'
rg -n 'stat -c%s|grep -q "\\$marker"|> "\\$SERIAL_LOG" 2>&1 \\|\\| true|-serial mon:stdio.*>.*build/serial[.]log' harness-engineering --glob '!**/06-validation/**' --glob '!**/11-reference/**'
```

If a match appears, fix the stale snippet before continuing.

## Snippet Validation

- Makefile snippets must include `OBJCOPY`.
- Makefile snippets must treat `KERNEL_DEFINES` or equivalent build config flags as dependencies through a stamp/config file.
- Makefile snippets must generate `boot_config.inc` from `kernel.bin` size.
- Makefile snippets must use `wc -c` for portable byte counts.
- Makefile snippets must guard `BUILD_DIR` and `OS_IMG` before `dd` or `clean`.
- Makefile snippets must validate `boot.bin` is exactly 512 bytes.
- Loader snippets must either keep sector 0 minimal and load `stage2.bin`, or explicitly justify any first-stage-only design.
- Boot sector snippets must use `nasm -f bin`.
- Boot sector snippets must initialize `DS`, `ES`, `SS`, and `SP` before memory/string/disk access.
- Kernel flow must name both `kernel.elf` and `kernel.bin`.
- QEMU snippets must use `-drive file=build/os.img,format=raw`.
- Automated QEMU snippets must use `-serial file:build/serial.log`, `-monitor none`, and `-nic none`.
- Human debug snippets may use `-serial mon:stdio`, but must state that it multiplexes COM1 with the monitor.
- Marker parsers must normalize CRLF and use exact whole-line matching.

## Negative Parser Fixtures

These serial logs must fail:

```text
NOT_BOOT_OK
KERNEL_INIT_OK
```

```text
BOOT_OK_FAKE
KERNEL_INIT_OK
```

```text
BOOT_OK
```

```text
BOOT_OK
KERNEL_INIT_OK
KERNEL_PANIC
```

## Regression Checklist

Before moving to a new OS feature:
- Previous required marker still appears.
- Optional marker not promoted to required until feature exists.
- Current shell phase keeps `SHELL_READY` required and also runs shell-runtime validation.
- New docs link back to artifact contract.
- New safety guidance does not weaken host disk/device restrictions.
- New skill has verification steps and failure behavior.
- Parser changes include negative fixtures.
- Evidence files are machine-written; prose summaries cannot be the only proof.
- Git state is reported: branch, status, diff summary, tracked-artifact verdict, and staged-deletion verdict.

## Risk-Targeted Checks

Borrowing the DRS-style idea of treating change risk as a gate, classify changes before accepting a pass:

| Risk area | Examples | Required evidence |
|---|---|---|
| High | bootloader, linker script, GDT, sector count, QEMU safety, marker parser, Git staging/commit/push policy | `make all`, `make test`, drift checks, artifact/marker evidence, Git preflight |
| Medium | serial driver, shell readiness, memory/state templates | Targeted test plus marker contract review |
| Medium/High | process, scheduler, syscall, paging, user mode | Dedicated runtime tests proving behavior, not only file presence or boot survival |
| Low | glossary wording, index links, non-contract examples | Link/path check and no stale-pattern hits |

High-risk changes cannot pass on prose review alone.

## Evidence Checklist

Each autonomous run should capture:
- Prompt/task.
- Files touched.
- Git branch/status/diff summary.
- Commands and exit status.
- Build artifact list.
- Artifact sizes and hashes.
- QEMU exit status and timeout verdict.
- Serial log hash.
- Sector-count verdict.
- Marker verdict.
- Safety verdict.
- Risk classification.
- Follow-up risks.
