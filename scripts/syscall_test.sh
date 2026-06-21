#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.syscall.log"
SERIAL_CLEAN="$BUILD_DIR/serial.syscall.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.syscall.log"
EVIDENCE_LOG="$BUILD_DIR/evidence.jsonl"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
TASK_NAME="${TASK_NAME:-syscall-abi-test}"
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
  shell_ready=false
  syscall_abi_ok=false
  marker_present "BOOT_OK" && boot_ok=true
  marker_present "KERNEL_INIT_OK" && kernel_ok=true
  marker_present "SHELL_READY" && shell_ready=true
  line_present "SYSCALL_ABI_OK:ARGS_OK" && syscall_abi_ok=true

  task_json="$(json_escape "$TASK_NAME")"
  printf '{"run_id":"%s","task":"%s","started_at":"%s","ended_at":"%s","qemu_status":%s,"artifacts":[{"path":"build/boot.bin","bytes":"%s","sha256":"%s"},{"path":"build/kernel.bin","bytes":"%s","sha256":"%s"}],"serial_log_sha256":"%s","markers":{"BOOT_OK":%s,"KERNEL_INIT_OK":%s,"SHELL_READY":%s},"subsystem":{"SYSCALL_ABI_OK":%s},"verdict":"%s"}\n' \
    "$run_id" "$task_json" "$started_at" "$ended_at" "$qemu_status" \
    "$boot_bytes" "$boot_sha" "$kernel_bytes" "$kernel_sha" "$serial_sha" \
    "$boot_ok" "$kernel_ok" "$shell_ready" "$syscall_abi_ok" "$verdict" >> "$EVIDENCE_LOG"
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
if ! marker_present "SHELL_READY"; then
  ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "fail"
  fail "missing SHELL_READY"
fi

echo "[PASS] BOOT_OK"
echo "[PASS] KERNEL_INIT_OK"
echo "[PASS] SHELL_READY"

if line_present "SYSCALL_ABI_OK:ARGS_OK"; then
  echo "[PASS] syscall ABI verified"
else
  ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "fail"
  fail "syscall ABI not verified - missing SYSCALL_ABI_OK:ARGS_OK"
fi

ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "pass"
echo "=== SYSCALL ABI TEST PASSED ==="
exit 0
