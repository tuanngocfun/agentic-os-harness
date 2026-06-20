#!/usr/bin/env bash
set -u

# Prove fork return semantics, blocking wait/reap, copy isolation, and exec replacement.

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.process_lifecycle.log"
SERIAL_CLEAN="$BUILD_DIR/serial.process_lifecycle.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.process_lifecycle.log"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
QEMU="${QEMU:-qemu-system-i386}"
QEMU_BIOS_DIR="${QEMU_BIOS_DIR:-/home/ngocnt/opt/share/qemu}"

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

marker_line() {
  clean_serial
  grep -n -m1 -Fx "$1" "$SERIAL_CLEAN" | cut -d: -f1
}

echo "=== Testing Fork/Wait/Exit And Exec Replacement ==="

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

case "$qemu_status" in
  124) ;;
  *) fail "qemu exited with status $qemu_status; see $QEMU_LOG" ;;
esac

marker_present "BOOT_OK" || fail "missing BOOT_OK"
marker_present "KERNEL_INIT_OK" || fail "missing KERNEL_INIT_OK"
failure_present "KERNEL_PANIC" && fail "found KERNEL_PANIC"
failure_present "LIFECYCLE_FAIL" && fail "lifecycle selftest failed"

for marker in \
  LIFECYCLE_TEST \
  LIFECYCLE_EXEC_FIXTURE_OK \
  LIFECYCLE_USER_READY \
  LIFECYCLE_FORK_PARENT_OK \
  LIFECYCLE_FORK_CHILD_OK \
  LIFECYCLE_WAIT_REAP_OK \
  LIFECYCLE_FORK_ISOLATION_OK \
  PROCESS_EXEC_LOAD_OK \
  LIFECYCLE_EXEC_REPLACEMENT_OK \
  LIFECYCLE_OK; do
  marker_present "$marker" || fail "missing $marker"
done

parent_line="$(marker_line LIFECYCLE_FORK_PARENT_OK)"
child_line="$(marker_line LIFECYCLE_FORK_CHILD_OK)"
wait_line="$(marker_line LIFECYCLE_WAIT_REAP_OK)"
exec_line="$(marker_line LIFECYCLE_EXEC_REPLACEMENT_OK)"

[ "$parent_line" -lt "$child_line" ] ||
  fail "child ran before parent observed the fork return"
[ "$child_line" -lt "$wait_line" ] ||
  fail "wait returned before the child exit path ran"
[ "$wait_line" -lt "$exec_line" ] ||
  fail "exec replacement marker appeared before wait/reap completed"

echo "[PASS] parent receives child PID and child receives zero"
echo "[PASS] wait blocks until child exits with status 42"
echo "[PASS] zombie child is reaped before parent resumes user work"
echo "[PASS] forked user stack/address-space writes are isolated"
echo "[PASS] exec replaces old mappings and resets heap metadata"
echo ""
echo "=== PROCESS LIFECYCLE TEST PASSED ==="
