#!/usr/bin/env bash
set -u

# Test ELF32 loader preparation on top of VFS-backed file reads.

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.elf_loader.log"
SERIAL_CLEAN="$BUILD_DIR/serial.elf_loader.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.elf_loader.log"
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

failure_present() {
  clean_serial
  grep -Eq "^$1(:|$)" "$SERIAL_CLEAN"
}

echo "=== Testing ELF Loader Preparation ==="

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
qemu_status=$?
set -e

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
failure_present "ELF_FAIL" && fail "ELF loader selftest failed"

marker_present "ELF_TEST" || fail "ELF loader test did not start"
marker_present "ELF_VFS_WRITE_OK" || fail "ELF fixtures were not written through VFS"
marker_present "ELF_LOAD_OK" || fail "valid ELF did not load"
marker_present "ELF_METADATA_OK" || fail "ELF load metadata mismatch"
marker_present "ELF_SEGMENT_OK" || fail "ELF PT_LOAD bytes not materialized"
marker_present "ELF_BSS_OK" || fail "ELF BSS zero-fill failed"
marker_present "ELF_NEGATIVE_OK" || fail "ELF negative paths failed"
marker_present "ELF_PREP_OK" || fail "ELF loader prep test incomplete"

echo "[PASS] ELF fixture files written through VFS"
echo "[PASS] ELF32/i386 executable parsed and loaded"
echo "[PASS] PT_LOAD bytes copied into user-mapped pages"
echo "[PASS] BSS zero-fill and metadata verified"
echo "[PASS] Invalid/truncated/missing ELF files rejected"
echo ""
echo "=== ELF LOADER PREP TEST PASSED ==="
