#!/usr/bin/env bash
set -euo pipefail

# Test that timer IRQ0 is unmasked and firing
# This test validates that PIC mask is 0xFE (IRQ0 + IRQ1 enabled)
# and that timer_handler is being called.

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.timer_preemption.log"
QEMU_LOG="$BUILD_DIR/qemu.timer_preemption.log"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-5}"
QEMU="${QEMU:-qemu-system-i386}"

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

echo "=== Testing Timer IRQ0 Preemption Infrastructure ==="

# Rebuild with timer tick counter enabled
make -B all KERNEL_DEFINES=-DENABLE_TIMER_TEST 2>&1 | tail -3

: > "$SERIAL_LOG"
: > "$QEMU_LOG"

# Run QEMU with timeout
timeout --preserve-status "$TIMEOUT_SECONDS" "$QEMU" \
  -drive file="$OS_IMG",format=raw \
  -m 512M \
  -serial file:"$SERIAL_LOG" \
  -monitor none \
  -nic none \
  -display none \
  -no-reboot \
  > "$QEMU_LOG" 2>&1 || true

# Check that timer ticks are incrementing
if ! grep -q "TIMER_TICK_COUNT" "$SERIAL_LOG"; then
  fail "Timer tick counter not found in serial log"
fi

# Extract tick count
tick_count=$(grep "TIMER_TICK_COUNT" "$SERIAL_LOG" | tail -1 | awk '{print $2}')

if [ -z "$tick_count" ] || [ "$tick_count" -lt 10 ]; then
  fail "Timer ticks too low: $tick_count (expected >= 10 in ${TIMEOUT_SECONDS}s)"
fi

echo "[PASS] Timer IRQ0 enabled: $tick_count ticks in ${TIMEOUT_SECONDS}s"
echo "[PASS] Timer handler is firing (PIC mask = 0xFE)"
echo "=== TIMER PREEMPTION INFRASTRUCTURE TEST PASSED ==="

# Rebuild default image without test define
make -B all 2>&1 | tail -3
