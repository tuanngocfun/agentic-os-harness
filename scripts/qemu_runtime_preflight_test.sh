#!/usr/bin/env bash
set -euo pipefail

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source scripts/qemu_runtime.sh

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

expect_failure() {
  description="$1"
  shift
  if ("$@") >/dev/null 2>&1; then
    fail "$description unexpectedly succeeded"
  fi
}

BASE_BIOS="${QEMU_BIOS_DIR:?QEMU_BIOS_DIR must be explicitly set}"
TEST_ROOT="build/qemu-preflight-$$"
OS_IMG="build/os.img"
BUILD_DIR="build"
SERIAL_LOG="$TEST_ROOT/serial.log"
SERIAL_CLEAN="$TEST_ROOT/serial.clean.log"
QEMU_LOG="$TEST_ROOT/qemu.log"
TASK_NAME="qemu-preflight-negative-test"
QEMU="qemu-system-i386"

mkdir -p "$TEST_ROOT"
[ -f "$OS_IMG" ] || fail "build/os.img is required"
[ -d "$BASE_BIOS" ] || fail "$BASE_BIOS is required for this validation session"

expect_failure "unset override" env -u QEMU_BIOS_DIR bash -c '
  source scripts/qemu_runtime.sh
  OS_IMG=build/os.img
  BUILD_DIR=build
  QEMU=qemu-system-i386
  qemu_runtime_preflight
'
echo "QEMU_PREFLIGHT_UNSET_BLOCKED"

QEMU_BIOS_DIR="$TEST_ROOT/missing"
expect_failure "missing BIOS directory" qemu_runtime_preflight
echo "QEMU_PREFLIGHT_MISSING_DIR_BLOCKED"

SYMLINK_DIR="$TEST_ROOT/symlink"
mkdir -p "$SYMLINK_DIR"
ln -s "$BASE_BIOS/bios-256k.bin" "$SYMLINK_DIR/bios-256k.bin"
ln -s "$BASE_BIOS/kvmvapic.bin" "$SYMLINK_DIR/kvmvapic.bin"
ln -s "$BASE_BIOS/vgabios-stdvga.bin" "$SYMLINK_DIR/vgabios-stdvga.bin"
QEMU_BIOS_DIR="$SYMLINK_DIR"
expect_failure "symlinked BIOS" qemu_runtime_preflight
echo "QEMU_PREFLIGHT_SYMLINK_BLOCKED"

TAMPER_DIR="$TEST_ROOT/tamper"
mkdir -p "$TAMPER_DIR"
cp "$BASE_BIOS/bios-256k.bin" "$TAMPER_DIR/bios-256k.bin"
cp "$BASE_BIOS/kvmvapic.bin" "$TAMPER_DIR/kvmvapic.bin"
cp "$BASE_BIOS/vgabios-stdvga.bin" "$TAMPER_DIR/vgabios-stdvga.bin"
printf 'X' | dd of="$TAMPER_DIR/bios-256k.bin" bs=1 seek=32 count=1 conv=notrunc status=none
QEMU_BIOS_DIR="$TAMPER_DIR"
expect_failure "tampered BIOS" qemu_runtime_preflight
echo "QEMU_PREFLIGHT_HASH_BLOCKED"

QEMU_BIOS_DIR="$BASE_BIOS"
QEMU="$TEST_ROOT/not-qemu"
expect_failure "missing QEMU" qemu_runtime_preflight
echo "QEMU_PREFLIGHT_MISSING_QEMU_BLOCKED"

FAKE_QEMU="$TEST_ROOT/fake-qemu"
printf '%s\n' '#!/usr/bin/env bash' 'echo "QEMU emulator version 0.0.0"' > "$FAKE_QEMU"
chmod +x "$FAKE_QEMU"
QEMU="$FAKE_QEMU"
expect_failure "unexpected QEMU version" qemu_runtime_preflight
echo "QEMU_PREFLIGHT_VERSION_BLOCKED"

QEMU="qemu-system-i386"
qemu_runtime_preflight
echo "QEMU_PREFLIGHT_VALID_OK"

printf 'stale-data\n' > "$SERIAL_LOG"
qemu_runtime_begin
[ ! -s "$SERIAL_LOG" ] || fail "qemu_runtime_begin did not truncate stale serial data"
echo "QEMU_PREFLIGHT_STALE_LOG_BLOCKED"

printf 'BOOT_OK\n' > "$SERIAL_LOG"
expect_failure "early timeout-mode exit" qemu_runtime_validate 0 timeout
echo "QEMU_PREFLIGHT_EARLY_EXIT_BLOCKED"

qemu_runtime_begin
expect_failure "empty serial log" qemu_runtime_validate 124 timeout
echo "QEMU_PREFLIGHT_EMPTY_SERIAL_BLOCKED"

printf 'OTHER_MARKER\n' > "$SERIAL_LOG"
expect_failure "missing exact marker" qemu_runtime_require_exact_marker BOOT_OK
echo "QEMU_PREFLIGHT_MISSING_MARKER_BLOCKED"

echo "QEMU_PREFLIGHT_OK"
