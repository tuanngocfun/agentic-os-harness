#!/usr/bin/env bash
set -euo pipefail

# Test syscall negative paths (invalid syscall numbers, bad pointers, permissions)

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.syscall_negative.log"
SERIAL_CLEAN="$BUILD_DIR/serial.syscall_negative.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.syscall_negative.log"
EVIDENCE_LOG="$BUILD_DIR/evidence.jsonl"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
TASK_NAME="${TASK_NAME:-syscall-negative-test}"
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

echo "=== Testing Syscall Negative Paths ==="

: > "$SERIAL_LOG"
: > "$QEMU_LOG"

timeout --preserve-status "$TIMEOUT_SECONDS" "$QEMU" \
  -drive file="$OS_IMG",format=raw \
  -m 512M \
  -serial file:"$SERIAL_LOG" \
  -monitor none \
  -nic none \
  -display none \
  -no-reboot \
  > "$QEMU_LOG" 2>&1 || true

marker_present "BOOT_OK" || fail "missing BOOT_OK"
marker_present "KERNEL_INIT_OK" || fail "missing KERNEL_INIT_OK"

# Check for negative path markers
marker_present "SYSCALL_NEGATIVE_TEST" || fail "syscall negative test did not start"
marker_present "SYSCALL_INVALID_NUM_OK" || fail "invalid syscall number not rejected"
marker_present "SYSCALL_BAD_POINTER_OK" || fail "bad pointer not rejected"
marker_present "SYSCALL_RING_CHECK_OK" || fail "ring privilege check failed"
marker_present "SYSCALL_NEGATIVE_OK" || fail "syscall negative tests incomplete"

echo "[PASS] Invalid syscall numbers rejected"
echo "[PASS] Bad pointers rejected"
echo "[PASS] Ring privilege validated"
echo "=== SYSCALL NEGATIVE PATH TEST PASSED ==="
