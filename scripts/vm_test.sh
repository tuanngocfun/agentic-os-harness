#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.vm.log"
SERIAL_CLEAN="$BUILD_DIR/serial.vm.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.vm.log"
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

echo "=== Testing COW, Demand Paging, Guard Pages, And Rollback ==="

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
failure_present "VM_FAIL" && fail "VM selftest reported VM_FAIL"

for marker in \
  BOOT_OK \
  KERNEL_INIT_OK \
  VM_TEST \
  VM_CLONE_ROLLBACK_OK \
  VM_COW_OOM_ROLLBACK_OK \
  VM_DEMAND_OOM_ROLLBACK_OK \
  VM_USER_READY \
  VM_DEMAND_ZERO_OK \
  VM_COW_SHARED_OK \
  VM_COW_SPLIT_OK \
  VM_COW_ISOLATION_OK \
  VM_FRAME_ACCOUNTING_OK \
  VM_GUARD_TERMINATION_OK \
  VM_OK; do
  marker_present "$marker" || fail "missing $marker"
done

echo "[PASS] two-phase clone leaves parent unchanged on allocation failure"
echo "[PASS] COW allocation failure preserves mapping and reference ownership"
echo "[PASS] demand allocation failure leaves no mapping or leaked frame"
echo "[PASS] fork shares writable user pages as read-only COW"
echo "[PASS] child write splits the page and preserves parent data"
echo "[PASS] lazy brk faults in a zeroed writable page"
echo "[PASS] child COW resources return to the frame baseline"
echo "[PASS] stack guard fault terminates only the offending process"
echo ""
echo "=== VM TEST PASSED ==="
