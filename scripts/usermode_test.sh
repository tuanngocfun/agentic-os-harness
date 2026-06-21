#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.usermode.log"
SERIAL_CLEAN="$BUILD_DIR/serial.usermode.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.usermode.log"
EVIDENCE_LOG="$BUILD_DIR/evidence.jsonl"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
TASK_NAME="${TASK_NAME:-usermode-ring3-paging-test}"
QEMU="${QEMU:-qemu-system-i386}"
QEMU_BIOS_DIR="${QEMU_BIOS_DIR:-}"
source "$(dirname "$0")/qemu_runtime.sh"

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

file_bytes() {
  wc -c < "$1" | tr -d ' '
}

file_sha256() {
  sha256sum "$1" | awk '{print $1}'
}

clean_serial() {
  tr -d '\r' < "$SERIAL_LOG" > "$SERIAL_CLEAN"
}

marker_present() {
  clean_serial
  grep -Fxq "$1" "$SERIAL_CLEAN"
}

panic_present() {
  clean_serial
  grep -Eq '^KERNEL_PANIC:0x0000000E:.*:0x00700000$' "$SERIAL_CLEAN"
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
  process_ready=false
  ring3_ok=false
  paging_fault_ok=false
  panic_ok=false
  marker_present "BOOT_OK" && boot_ok=true
  marker_present "KERNEL_INIT_OK" && kernel_ok=true
  marker_present "PROCESS_USERMODE_READY" && process_ready=true
  marker_present "USERMODE_RING3_OK" && ring3_ok=true
  marker_present "PAGING_USER_SUPERVISOR_FAULT_OK" && paging_fault_ok=true
  panic_present && panic_ok=true

  task_json="$(json_escape "$TASK_NAME")"
  printf '{"run_id":"%s","task":"%s","started_at":"%s","ended_at":"%s","qemu_status":%s,"artifacts":[{"path":"build/boot.bin","bytes":"%s","sha256":"%s"},{"path":"build/kernel.bin","bytes":"%s","sha256":"%s"}],"serial_log_sha256":"%s","markers":{"BOOT_OK":%s,"KERNEL_INIT_OK":%s},"subsystem":{"PROCESS_USERMODE_READY":%s,"USERMODE_RING3_OK":%s,"PAGING_USER_SUPERVISOR_FAULT_OK":%s,"KERNEL_PANIC_PAGEFAULT_0x00700000":%s},"verdict":"%s"}\n' \
    "$run_id" "$task_json" "$started_at" "$ended_at" "$qemu_status" \
    "$boot_bytes" "$boot_sha" "$kernel_bytes" "$kernel_sha" "$serial_sha" \
    "$boot_ok" "$kernel_ok" "$process_ready" "$ring3_ok" "$paging_fault_ok" "$panic_ok" "$verdict" >> "$EVIDENCE_LOG"
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

for marker in BOOT_OK KERNEL_INIT_OK PROCESS_USERMODE_READY USERMODE_RING3_OK PAGING_USER_SUPERVISOR_FAULT_OK; do
  if ! marker_present "$marker"; then
    write_evidence "$run_id" "$started_at" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$qemu_status" "fail"
    fail "missing $marker"
  fi
  echo "[PASS] $marker"
done

if ! panic_present; then
  write_evidence "$run_id" "$started_at" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$qemu_status" "fail"
  fail "missing user/supervisor page-fault panic for 0x00700000"
fi
echo "[PASS] ring-3 supervisor-page access produced structured page fault"

ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "pass"
echo "=== USERMODE RING3 PAGING TEST PASSED ==="
exit 0
