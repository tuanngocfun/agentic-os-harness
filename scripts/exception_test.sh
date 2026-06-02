#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.exception.log"
SERIAL_CLEAN="$BUILD_DIR/serial.exception.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.exception.log"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
TASK_NAME="${TASK_NAME:-exception-panic-test}"
QEMU="${QEMU:-qemu-system-i386}"

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

clean_serial() {
  tr -d '\r' < "$SERIAL_LOG" > "$SERIAL_CLEAN"
}

marker_present() {
  clean_serial
  grep -Fxq "$1" "$SERIAL_CLEAN"
}

line_present() {
  clean_serial
  grep -Fq "$1" "$SERIAL_CLEAN"
}

panic_present() {
  clean_serial
  grep -Eq '^KERNEL_PANIC:0x00000006:0x00000000$' "$SERIAL_CLEAN"
}

[ "$BUILD_DIR" = "build" ] || fail "BUILD_DIR must be build"
[ "$OS_IMG" = "build/os.img" ] || fail "OS_IMG must be build/os.img"
[ -f "$OS_IMG" ] || fail "$OS_IMG not found; run make all first"
command -v "$QEMU" >/dev/null 2>&1 || fail "$QEMU not found in PATH"

mkdir -p "$BUILD_DIR"
: > "$SERIAL_LOG"
: > "$SERIAL_CLEAN"
: > "$QEMU_LOG"

set +e
timeout "$TIMEOUT_SECONDS" "$QEMU" \
  -drive file="$OS_IMG",format=raw \
  -m 512M \
  -serial file:"$SERIAL_LOG" \
  -monitor none \
  -nic none \
  -display none \
  -no-reboot \
  > "$QEMU_LOG" 2>&1
qemu_status=$?
set -e

clean_serial

echo "--- Serial Output ---"
cat "$SERIAL_CLEAN"
echo "--- End Serial Output ---"
echo ""

marker_present "BOOT_OK" || fail "missing BOOT_OK"
marker_present "KERNEL_INIT_OK" || fail "missing KERNEL_INIT_OK"
echo "[PASS] BOOT_OK"
echo "[PASS] KERNEL_INIT_OK"

if panic_present; then
  echo "[PASS] invalid-opcode exception produced structured KERNEL_PANIC"
else
  fail "missing structured invalid-opcode panic marker KERNEL_PANIC:0x00000006:0x00000000"
fi

echo "=== EXCEPTION PANIC TEST PASSED ==="
exit 0
