#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.ipc.log"
SERIAL_CLEAN="$BUILD_DIR/serial.ipc.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.ipc.log"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-12}"
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

echo "=== Testing Waitpid, Signal Delivery, and Blocking Pipes ==="

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

failure_present "KERNEL_PANIC" && fail "found KERNEL_PANIC"
failure_present "IPC_FAIL" && fail "IPC selftest reported IPC_FAIL"

for marker in \
  BOOT_OK \
  KERNEL_INIT_OK \
  IPC_TEST \
  IPC_USER_READY \
  WAITPID_WNOHANG_OK \
  WAITPID_SPECIFIC_OK \
  WAITPID_STATUS_OK \
  WAITPID_NEGATIVE_OK \
  SIGNAL_KILL_OK \
  SIGNAL_IGN_OK \
  PIPE_CREATE_OK \
  PIPE_IO_OK \
  PIPE_EOF_OK \
  IPC_OK; do
  marker_present "$marker" || fail "missing $marker"
done

echo "[PASS] waitpid WNOHANG behaves correctly when child is running"
echo "[PASS] waitpid reaps a specific child and blocking wait reclaims resource"
echo "[PASS] waitpid reaps child status with correct POSIX encoding"
echo "[PASS] waitpid returns ECHILD for non-existent child process"
echo "[PASS] signal delivery terminates the target process on user space transition"
echo "[PASS] signal SYS_KILL returns ESRCH and EINVAL for bad arguments"
echo "[PASS] pipe creation returns correct read/write handles"
echo "[PASS] pipe IO transfers data correctly from child writer to parent reader"
echo "[PASS] pipe EOF is correctly detected when write ends are closed"
echo ""
echo "=== IPC TEST PASSED ==="
