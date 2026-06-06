#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.scheduler_safety.log"
SERIAL_CLEAN="$BUILD_DIR/serial.scheduler_safety.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.scheduler_safety.log"
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
set -e

clean_serial

echo "--- Serial Output ---"
cat "$SERIAL_CLEAN"
echo "--- End Serial Output ---"
echo ""

for marker in BOOT_OK KERNEL_INIT_OK SCHED_SAFETY_TEST SCHED_YIELD_GUARD_OK SCHED_PRIORITY_OK SCHED_FAIRNESS_OK SCHED_CRITICAL_OK SCHED_SAFETY_OK; do
  if ! marker_present "$marker"; then
    fail "missing $marker"
  fi
  echo "[PASS] $marker"
done

if marker_present "SCHED_SAFETY_FAIL"; then
  fail "scheduler safety selftest reported SCHED_SAFETY_FAIL"
fi

echo "=== SCHEDULER SAFETY TEST PASSED ==="
exit 0
