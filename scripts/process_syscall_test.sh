#!/usr/bin/env bash
set -u

# Test ring-3 process syscalls and VFS-backed ELF entry transfer.

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.process_syscall.log"
SERIAL_CLEAN="$BUILD_DIR/serial.process_syscall.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.process_syscall.log"
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

echo "=== Testing Process Syscalls And ELF Entry Transfer ==="

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
failure_present "PROCESS_FAIL" && fail "process syscall selftest failed"

marker_present "PROCESS_SYSCALL_TEST" || fail "process syscall test did not start"
marker_present "PROCESS_EXEC_FIXTURE_OK" || fail "ELF fixture was not written through VFS"
marker_present "PROCESS_USER_READY" || fail "ring-3 process setup did not complete"
marker_present "PROCESS_GETPID_OK" || fail "SYS_GETPID did not work from ring 3"
marker_present "PROCESS_BRK_QUERY_OK" || fail "SYS_BRK query failed"
marker_present "PROCESS_BRK_GROW_OK" || fail "SYS_BRK grow failed"
marker_present "PROCESS_BRK_RW_OK" || fail "grown heap pages were not readable/writable"
marker_present "PROCESS_BRK_SHRINK_OK" || fail "SYS_BRK shrink failed"
marker_present "PROCESS_WAIT_NEGATIVE_OK" || fail "SYS_WAIT no-child negative path failed"
marker_present "PROCESS_EXEC_LOAD_OK" || fail "SYS_EXEC did not load ELF through VFS"
marker_present "PROCESS_EXEC_ENTERED_OK" || fail "SYS_EXEC did not transfer control to ELF entry"
marker_present "PROCESS_OK" || fail "process syscall test incomplete"

echo "[PASS] SYS_GETPID from ring 3"
echo "[PASS] SYS_BRK query/grow/read-write/shrink"
echo "[PASS] SYS_WAIT no-child negative path"
echo "[PASS] SYS_EXEC loads VFS-backed ELF and enters its ring-3 entry"
echo ""
echo "=== PROCESS SYSCALL TEST PASSED ==="
