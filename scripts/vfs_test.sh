#!/usr/bin/env bash
set -u

# Test VFS + SimpleFS functionality on top of the ramdisk block device.

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.vfs.log"
SERIAL_CLEAN="$BUILD_DIR/serial.vfs.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.vfs.log"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
QEMU="${QEMU:-qemu-system-i386}"
QEMU_BIOS_DIR="${QEMU_BIOS_DIR:-}"
source "$(dirname "$0")/qemu_runtime.sh"

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

echo "=== Testing VFS + SimpleFS ==="

[ "$BUILD_DIR" = "build" ] || fail "BUILD_DIR must be build"
[ "$OS_IMG" = "build/os.img" ] || fail "$OS_IMG must be build/os.img"
[ -f "$OS_IMG" ] || fail "$OS_IMG not found; run make all first"
command -v "$QEMU" >/dev/null 2>&1 || fail "$QEMU not found in PATH"

mkdir -p "$BUILD_DIR"
: > "$SERIAL_LOG"
: > "$SERIAL_CLEAN"
: > "$QEMU_LOG"

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

case "$qemu_status" in
  124) ;;
  *) fail "qemu exited with status $qemu_status; see $QEMU_LOG" ;;
esac

marker_present "BOOT_OK" || fail "missing BOOT_OK"
marker_present "KERNEL_INIT_OK" || fail "missing KERNEL_INIT_OK"
failure_present "KERNEL_PANIC" && fail "found KERNEL_PANIC"
failure_present "RAMDISK_INIT_FAIL" && fail "ramdisk init failed"
failure_present "RAMDISK_INIT_FATAL" && fail "ramdisk fatal init failure"
failure_present "VFS_FAIL" && fail "VFS selftest reported VFS_FAIL"

marker_present "VFS_TEST" || fail "VFS test did not start"
marker_present "VFS_FORMAT_OK" || fail "VFS format failed"
marker_present "VFS_MOUNT_OK" || fail "VFS mount failed"
marker_present "VFS_CREATE_OK" || fail "VFS create/open failed"
marker_present "VFS_WRITE_OK" || fail "VFS write failed"
marker_present "VFS_READ_OK" || fail "VFS read failed"
marker_present "VFS_OFFSET_OK" || fail "VFS file offset handling failed"
marker_present "VFS_STAT_OK" || fail "VFS stat failed"
marker_present "VFS_NEGATIVE_OK" || fail "VFS negative paths failed"
marker_present "VFS_OK" || fail "VFS test incomplete"

echo "[PASS] VFS format and mount"
echo "[PASS] SimpleFS create/write/read"
echo "[PASS] File descriptor offsets"
echo "[PASS] Metadata stat"
echo "[PASS] Negative paths"
echo ""
echo "=== VFS TEST PASSED ==="
