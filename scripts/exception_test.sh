#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.exception.log"
SERIAL_CLEAN="$BUILD_DIR/serial.exception.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.exception.log"
EVIDENCE_LOG="$BUILD_DIR/evidence.jsonl"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
QEMU="${QEMU:-qemu-system-i386}"
QEMU_BIOS_DIR="${QEMU_BIOS_DIR:-}"
source "$(dirname "$0")/qemu_runtime.sh"

EXCEPTION_VECTOR="${EXCEPTION_VECTOR:-6}"

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

case "$EXCEPTION_VECTOR" in
  0)  TASK_NAME="${TASK_NAME:-exception-div0-test}"
      EXPECTED_PATTERN='^KERNEL_PANIC:0x00000000:0x00000000$'
      EXCEPTION_NAME="divide-by-zero"
      ;;
  6)  TASK_NAME="${TASK_NAME:-exception-panic-test}"
      EXPECTED_PATTERN='^KERNEL_PANIC:0x00000006:0x00000000$'
      EXCEPTION_NAME="invalid-opcode"
      ;;
  13) TASK_NAME="${TASK_NAME:-exception-gpf-test}"
      EXPECTED_PATTERN='^KERNEL_PANIC:0x0000000D:'
      EXCEPTION_NAME="general-protection-fault"
      ;;
  14) TASK_NAME="${TASK_NAME:-exception-pagefault-test}"
      EXPECTED_PATTERN='^KERNEL_PANIC:0x0000000E:'
      EXCEPTION_NAME="page-fault"
      ;;
  *)  fail "unsupported exception vector: $EXCEPTION_VECTOR (supported: 0, 6, 13, 14)"
      ;;
esac

json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

file_bytes() {
  wc -c < "$1" | tr -d ' '
}

file_sha256() {
  sha256sum "$1" | awk '{print $1}'
}

write_evidence() {
  run_id="$1"
  started_at="$2"
  ended_at="$3"
  qemu_status="$4"
  verdict="$5"

  boot_bytes="missing"
  boot_sha="missing"
  kernel_bytes="missing"
  kernel_sha="missing"
  serial_sha="missing"

  [ -f "$BUILD_DIR/boot.bin" ] && boot_bytes="$(file_bytes "$BUILD_DIR/boot.bin")" && boot_sha="$(file_sha256 "$BUILD_DIR/boot.bin")"
  [ -f "$BUILD_DIR/kernel.bin" ] && kernel_bytes="$(file_bytes "$BUILD_DIR/kernel.bin")" && kernel_sha="$(file_sha256 "$BUILD_DIR/kernel.bin")"
  [ -f "$SERIAL_LOG" ] && serial_sha="$(file_sha256 "$SERIAL_LOG")"

  boot_ok=false
  kernel_ok=false
  kernel_panic=false
  marker_present "BOOT_OK" && boot_ok=true
  marker_present "KERNEL_INIT_OK" && kernel_ok=true
  panic_present && kernel_panic=true

  task_json="$(json_escape "$TASK_NAME")"
  printf '{"run_id":"%s","task":"%s","started_at":"%s","ended_at":"%s","qemu_status":%s,"artifacts":[{"path":"build/boot.bin","bytes":"%s","sha256":"%s"},{"path":"build/kernel.bin","bytes":"%s","sha256":"%s"}],"serial_log_sha256":"%s","markers":{"BOOT_OK":%s,"KERNEL_INIT_OK":%s},"subsystem":{"KERNEL_PANIC_vector_%s":%s},"verdict":"%s"}\n' \
    "$run_id" "$task_json" "$started_at" "$ended_at" "$qemu_status" \
    "$boot_bytes" "$boot_sha" "$kernel_bytes" "$kernel_sha" "$serial_sha" \
    "$boot_ok" "$kernel_ok" "$EXCEPTION_VECTOR" "$kernel_panic" "$verdict" >> "$EVIDENCE_LOG"
}

clean_serial() {
  tr -d '\r' < "$SERIAL_LOG" > "$SERIAL_CLEAN"
}

marker_present() {
  clean_serial
  grep -Fxq "$1" "$SERIAL_CLEAN"
}

line_present() {
  clean_serial
  grep -Fq "$1" "$SERIAL_CLEAN"
}

panic_present() {
  clean_serial
  grep -Eq "$EXPECTED_PATTERN" "$SERIAL_CLEAN"
}

[ "$BUILD_DIR" = "build" ] || fail "BUILD_DIR must be build"
[ "$OS_IMG" = "build/os.img" ] || fail "OS_IMG must be build/os.img"
[ -f "$OS_IMG" ] || fail "$OS_IMG not found; run make all first"
command -v "$QEMU" >/dev/null 2>&1 || fail "$QEMU not found in PATH"

mkdir -p "$BUILD_DIR"
: > "$SERIAL_LOG"
: > "$SERIAL_CLEAN"
: > "$QEMU_LOG"

run_id="$(date -u +%Y%m%dT%H%M%SZ)-$$"
started_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

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

clean_serial

echo "--- Serial Output ---"
cat "$SERIAL_CLEAN"
echo "--- End Serial Output ---"
echo ""

if ! marker_present "BOOT_OK"; then
  ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "fail"
  fail "missing BOOT_OK"
fi
if ! marker_present "KERNEL_INIT_OK"; then
  ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "fail"
  fail "missing KERNEL_INIT_OK"
fi
echo "[PASS] BOOT_OK"
echo "[PASS] KERNEL_INIT_OK"

if panic_present; then
  echo "[PASS] $EXCEPTION_NAME exception produced structured KERNEL_PANIC"
else
  ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "fail"
  fail "missing structured $EXCEPTION_NAME panic marker (vector $EXCEPTION_VECTOR)"
fi

ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "pass"
echo "=== EXCEPTION PANIC TEST PASSED ==="
exit 0
