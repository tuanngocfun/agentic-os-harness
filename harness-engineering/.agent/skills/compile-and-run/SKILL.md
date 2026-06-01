# compile-and-run

Use this skill when the task is to build, run, or validate the x86 teaching OS boot path.

## Inputs

- `Makefile`
- `boot/boot.asm`
- `kernel/entry.asm`
- `kernel/*.c`
- `include/*.h`
- `linker.ld`
- `.agent/skills/compile-and-run/scripts/boot_test.sh`

## Contract

- `make all` must create `build/boot.bin`, `build/boot_config.inc`, `build/kernel.elf`, `build/kernel.bin`, and `build/os.img`.
- `build/boot.bin` must be exactly 512 bytes.
- Phase-1 CHS boot must keep `KERNEL_SECTORS <= 17`; larger kernels require track-rolling CHS, LBA, or 2-stage boot.
- Automated QEMU test must use a dedicated serial file: `-serial file:build/serial.log -monitor none -nic none`.
- Required markers are exact lines: `BOOT_OK`, `KERNEL_INIT_OK`.
- Failure markers are exact lines: `BOOT_DISK_ERROR`, `KERNEL_PANIC`.

## Steps

1. Run `make all`.
2. Run `.agent/skills/compile-and-run/scripts/boot_test.sh`.
3. Report command exit statuses, artifact sizes, marker verdict, QEMU status, and evidence file path.
4. Do not claim success from partial output, stale logs, or substring marker matches.

## Evidence

The boot test script appends machine evidence to `build/evidence.jsonl`.
