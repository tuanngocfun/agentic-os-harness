# 06 — Validation

## Mục tiêu

Validation biến harness từ docs thành feedback loop. Một run chỉ pass khi build artifacts đúng, serial markers đúng, regression không bị phá, và QEMU safety được giữ.

## Boot-Test Protocol

Required artifacts:
- `build/boot.bin`
- `build/boot_config.inc`
- `build/kernel.elf`
- `build/kernel.bin`
- `build/os.img`
- `build/serial.log` after test
- `build/qemu.log` after test

Required markers:
- `BOOT_OK`
- `KERNEL_INIT_OK`

Optional markers:
- `SHELL_READY`
- `TESTS_PASS`

Failure markers:
- `BOOT_DISK_ERROR`
- `KERNEL_PANIC`

Pass condition:
1. `make all` exits 0.
2. `make test` exits 0.
3. `build/serial.log` contains both required markers.
4. `build/serial.log` does not contain failure markers.
5. `build/boot.bin` is exactly 512 bytes.
6. `build/boot_config.inc` sector count matches `ceil(size(build/kernel.bin) / 512)`.
7. For the phase-1 CHS loader, generated sector count is `<= 17`.
8. `build/serial.log` was created/truncated by the current run, not reused from a stale pass.

## Marker Parser

Minimal shell parser:

```bash
required="BOOT_OK KERNEL_INIT_OK"
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
- Makefile snippets must generate `boot_config.inc` from `kernel.bin` size.
- Makefile snippets must use `wc -c` for portable byte counts.
- Makefile snippets must guard `BUILD_DIR` and `OS_IMG` before `dd` or `clean`.
- Makefile snippets must validate `boot.bin` is exactly 512 bytes.
- Phase-1 CHS snippets must reject `KERNEL_SECTORS > 17`, or must implement track-rolling CHS/LBA/2-stage loading.
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
- New docs link back to artifact contract.
- New safety guidance does not weaken host disk/device restrictions.
- New skill has verification steps and failure behavior.
- Parser changes include negative fixtures.
- Evidence files are machine-written; prose summaries cannot be the only proof.

## Risk-Targeted Checks

Borrowing the DRS-style idea of treating change risk as a gate, classify changes before accepting a pass:

| Risk area | Examples | Required evidence |
|---|---|---|
| High | bootloader, linker script, GDT, sector count, QEMU safety, marker parser | `make all`, `make test`, drift checks, artifact/marker evidence |
| Medium | serial driver, shell readiness, memory/state templates | Targeted test plus marker contract review |
| Low | glossary wording, index links, non-contract examples | Link/path check and no stale-pattern hits |

High-risk changes cannot pass on prose review alone.

## Evidence Checklist

Each autonomous run should capture:
- Prompt/task.
- Files touched.
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
