#!/usr/bin/env bash
set -u

# Test ring-3 file syscalls backed by the VFS + SimpleFS ramdisk gate.

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.syscall_file.log"
SERIAL_CLEAN="$BUILD_DIR/serial.syscall_file.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.syscall_file.log"
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

echo "=== Testing Ring-3 File Syscalls ==="

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
failure_present "VFS_INIT_FATAL" && fail "VFS fatal init failure"
failure_present "SYSCALL_FILE_FAIL" && fail "file syscall selftest failed"
failure_present "SYSCALL_FILE_OPEN_FAIL" && fail "SYS_OPEN failed from ring 3"
failure_present "SYSCALL_FILE_WRITE_FAIL" && fail "SYS_WRITE/SYS_CLOSE failed from ring 3"
failure_present "SYSCALL_FILE_READ_FAIL" && fail "SYS_READ failed from ring 3"
failure_present "SYSCALL_FILE_STAT_FAIL" && fail "SYS_STAT failed from ring 3"
failure_present "SYSCALL_FILE_NEGATIVE_FAIL" && fail "file syscall negative paths failed"

marker_present "SYSCALL_FILE_TEST" || fail "file syscall test did not start"
marker_present "SYSCALL_FILE_OPEN_OK" || fail "SYS_OPEN did not work from ring 3"
marker_present "SYSCALL_FILE_WRITE_OK" || fail "SYS_WRITE/SYS_CLOSE did not work from ring 3"
marker_present "SYSCALL_FILE_READ_OK" || fail "SYS_READ offset/data path failed from ring 3"
marker_present "SYSCALL_FILE_STAT_OK" || fail "SYS_STAT did not work from ring 3"
marker_present "SYSCALL_FILE_NEGATIVE_OK" || fail "file syscall negative paths failed"
marker_present "SYSCALL_FILE_OK" || fail "file syscall tests incomplete"

echo "[PASS] SYS_OPEN from ring 3"
echo "[PASS] SYS_WRITE/SYS_CLOSE through VFS"
echo "[PASS] SYS_READ offsets and data integrity"
echo "[PASS] SYS_STAT metadata copy-out"
echo "[PASS] File syscall negative paths"
echo ""
echo "=== FILE SYSCALL TEST PASSED ==="
