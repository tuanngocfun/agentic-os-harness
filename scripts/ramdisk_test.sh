#!/usr/bin/env bash
set -u

# Test ramdisk block device functionality

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.ramdisk.log"
SERIAL_CLEAN="$BUILD_DIR/serial.ramdisk.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.ramdisk.log"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
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

failure_present() {
  clean_serial
  grep -Eq "^$1(:|$)" "$SERIAL_CLEAN"
}

echo "=== Testing Ramdisk Block Device ==="

[ "$BUILD_DIR" = "build" ] || fail "BUILD_DIR must be build"
[ "$OS_IMG" = "build/os.img" ] || fail "$OS_IMG must be build/os.img"
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

case "$qemu_status" in
  124) ;;
  *) fail "qemu exited with status $qemu_status; see $QEMU_LOG" ;;
esac

marker_present "BOOT_OK" || fail "missing BOOT_OK"
marker_present "KERNEL_INIT_OK" || fail "missing KERNEL_INIT_OK"
failure_present "KERNEL_PANIC" && fail "found KERNEL_PANIC"
failure_present "RAMDISK_INIT_FAIL" && fail "ramdisk init failed"
failure_present "RAMDISK_INIT_FATAL" && fail "ramdisk fatal init failure"
failure_present "RAMDISK_FAIL" && fail "ramdisk selftest reported RAMDISK_FAIL"

marker_present "RAMDISK_TEST" || fail "ramdisk test did not start"
marker_present "RAMDISK_INIT_OK" || fail "ramdisk init marker missing"
marker_present "RAMDISK_DEVICE_OK" || fail "ramdisk device metadata invalid"
marker_present "RAMDISK_BASIC_OK" || fail "ramdisk basic read/write test failed"
marker_present "RAMDISK_MULTI_OK" || fail "ramdisk multi-sector test failed"
marker_present "RAMDISK_BOUNDS_OK" || fail "ramdisk bounds check failed"
marker_present "RAMDISK_OK" || fail "ramdisk test incomplete"

echo "[PASS] Ramdisk initialized"
echo "[PASS] Ramdisk device metadata"
echo "[PASS] Ramdisk basic read/write"
echo "[PASS] Ramdisk multi-sector operations"
echo "[PASS] Ramdisk bounds checking"
echo ""
echo "=== RAMDISK TEST PASSED ==="
