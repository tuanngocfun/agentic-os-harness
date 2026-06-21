#!/usr/bin/env bash
set -euo pipefail

# Test E820 memory map detection

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.e820.log"
SERIAL_CLEAN="$BUILD_DIR/serial.e820.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.e820.log"
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

echo "=== Testing E820 Memory Map Detection ==="

: > "$SERIAL_LOG"
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

marker_present "BOOT_OK" || fail "missing BOOT_OK"
marker_present "KERNEL_INIT_OK" || fail "missing KERNEL_INIT_OK"

# Check for E820 test markers
marker_present "E820_TEST" || fail "E820 test did not start"
marker_present "E820_DETECT_OK" || fail "E820 detection failed"
marker_present "E820_USABLE_MEMORY_OK" || fail "E820 usable memory check failed"
marker_present "FRAME_ALLOC_OK" || fail "frame allocation failed"
marker_present "FRAME_FREE_OK" || fail "frame free accounting failed"
marker_present "FRAME_REUSE_OK" || fail "frame reuse failed"
marker_present "FRAME_EXHAUST_OK" || fail "low-frame exhaustion failed"
marker_present "E820_FRAME_OK" || fail "E820/frame test incomplete"

echo "[PASS] E820 memory map detected"
echo "[PASS] E820 usable memory validated"
echo "[PASS] Frame allocation/free/reuse/exhaustion validated"

# Show E820 map output
clean_serial
echo ""
echo "=== E820 Memory Map ==="
grep "E820" "$SERIAL_CLEAN" | head -20

echo ""
echo "=== E820 TEST PASSED ==="
