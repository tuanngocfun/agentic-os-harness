#!/usr/bin/env bash
set -u

# Prove per-process descriptor isolation, fork sharing, CLOEXEC, and exit cleanup.

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.process_fd.log"
SERIAL_CLEAN="$BUILD_DIR/serial.process_fd.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.process_fd.log"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-8}"
QEMU="${QEMU:-qemu-system-i386}"
QEMU_BIOS_DIR="${QEMU_BIOS_DIR:-/home/ngocnt/opt/share/qemu}"

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

marker_present() {
  grep -Fxq "$1" "$SERIAL_CLEAN"
}

failure_present() {
  grep -Eq "^$1(:|$)" "$SERIAL_CLEAN"
}

echo "=== Testing Per-Process File Descriptors ==="

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

[ "$qemu_status" -eq 124 ] ||
  fail "qemu exited with status $qemu_status; see $QEMU_LOG"

tr -d '\r' < "$SERIAL_LOG" > "$SERIAL_CLEAN"

marker_present "STAGE2_OK" || fail "missing STAGE2_OK"
marker_present "BOOT_OK" || fail "missing BOOT_OK"
marker_present "KERNEL_INIT_OK" || fail "missing KERNEL_INIT_OK"
failure_present "KERNEL_PANIC" && fail "found KERNEL_PANIC"
failure_present "PROCESS_FD_FAIL" && fail "descriptor selftest failed"

for marker in \
  PROCESS_FD_TEST \
  PROCESS_FD_ISOLATION_OK \
  PROCESS_FD_FORK_INHERIT_OK \
  PROCESS_FD_SHARED_OFFSET_OK \
  PROCESS_FD_CLOEXEC_OK \
  PROCESS_FD_EXIT_CLEANUP_OK \
  PROCESS_FD_NEGATIVE_OK \
  PROCESS_FD_OK; do
  marker_present "$marker" || fail "missing $marker"
done

echo "[PASS] descriptors are process-local before inheritance"
echo "[PASS] fork inheritance retains a shared open-file description and offset"
echo "[PASS] child close does not invalidate the parent's retained descriptor"
echo "[PASS] close-on-exec is selective and runs only after successful exec preparation"
echo "[PASS] exit closes all descriptors before zombie reap"
echo "[PASS] invalid local descriptors return EBADF"
echo ""
echo "=== PROCESS FD TEST PASSED ==="
