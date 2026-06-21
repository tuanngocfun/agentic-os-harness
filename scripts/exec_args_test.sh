#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.exec_args.log"
SERIAL_CLEAN="$BUILD_DIR/serial.exec_args.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.exec_args.log"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
TASK_NAME="${TASK_NAME:-exec-args-test}"
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

marker_line() {
  clean_serial
  grep -n -m1 -Fx "$1" "$SERIAL_CLEAN" | cut -d: -f1
}

[ "$BUILD_DIR" = "build" ] || fail "BUILD_DIR must be build"
[ "$OS_IMG" = "build/os.img" ] || fail "OS_IMG must be build/os.img"
[ -f "$OS_IMG" ] || fail "$OS_IMG not found; run make all first"

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

for marker in   BOOT_OK   KERNEL_INIT_OK   EXEC_ARGS_TEST   EXEC_ARGS_FIXTURE_OK   EXEC_ARGS_USER_READY   EXEC_ARGS_NEGATIVE_OK   PROCESS_EXEC_LOAD_OK   EXEC_ARGS_LAYOUT_OK   EXEC_ARGS_CONTENT_OK   EXEC_ARGS_OK; do
  marker_present "$marker" || fail "missing $marker"
done

if marker_present "EXEC_ARGS_FAIL"; then
  fail "exec argument selftest reported EXEC_ARGS_FAIL"
fi

negative_line="$(marker_line EXEC_ARGS_NEGATIVE_OK)"
load_line="$(marker_line PROCESS_EXEC_LOAD_OK)"
layout_line="$(marker_line EXEC_ARGS_LAYOUT_OK)"
content_line="$(marker_line EXEC_ARGS_CONTENT_OK)"
done_line="$(marker_line EXEC_ARGS_OK)"

[ "$negative_line" -lt "$load_line" ] ||
  fail "valid exec committed before negative-vector rollback checks"
[ "$load_line" -lt "$layout_line" ] ||
  fail "layout marker appeared before exec committed"
[ "$layout_line" -lt "$content_line" ] ||
  fail "content marker appeared before stack layout validation"
[ "$content_line" -lt "$done_line" ] ||
  fail "completion marker appeared before content validation"

echo "[PASS] malformed and oversized vectors leave the old image running"
echo "[PASS] exec installs a 16-byte-aligned argc/argv/envp stack"
echo "[PASS] the new ring-3 image validates argument and environment contents"
echo "=== EXEC ARGS TEST PASSED ==="
