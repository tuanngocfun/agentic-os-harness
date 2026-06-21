#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.marker_surface.log"
SERIAL_CLEAN="$BUILD_DIR/serial.marker_surface.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.marker_surface.log"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-8}"
TASK_NAME="${TASK_NAME:-marker-surface-test}"
QEMU="${QEMU:-qemu-system-i386}"
QEMU_BIOS_DIR="${QEMU_BIOS_DIR:-}"
source "$(dirname "$0")/qemu_runtime.sh"

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

marker_present() {
  tr -d '\r' < "$SERIAL_LOG" > "$SERIAL_CLEAN"
  grep -Fxq "$1" "$SERIAL_CLEAN"
}

[ "$BUILD_DIR" = "build" ] || fail "BUILD_DIR must be build"
[ "$OS_IMG" = "build/os.img" ] || fail "OS_IMG must be build/os.img"
[ -f "$OS_IMG" ] || fail "$OS_IMG not found; run make all first"

qemu_runtime_preflight
qemu_runtime_begin

set +e
timeout "$TIMEOUT_SECONDS" "$QEMU" \
  -L "$QEMU_BIOS_DIR" \
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

qemu_runtime_verify "$qemu_status" timeout

for marker in BOOT_OK KERNEL_INIT_OK MARKER_SURFACE_TEST MARKER_SURFACE_ENOSYS_OK; do
  marker_present "$marker" || fail "missing $marker"
done

if marker_present "MARKER_SURFACE_FAIL"; then
  fail "default syscall surface exposed SYS_TEST_MARKER"
fi

echo "[PASS] default syscall surface returns SYSCALL_ENOSYS for SYS_TEST_MARKER"
echo "=== MARKER SURFACE TEST PASSED ==="
