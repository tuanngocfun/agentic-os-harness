#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.timer_preemption.log"
SERIAL_CLEAN="$BUILD_DIR/serial.timer_preemption.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.timer_preemption.log"
EVIDENCE_LOG="$BUILD_DIR/evidence.jsonl"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
TASK_NAME="${TASK_NAME:-timer-preemption-test}"
QEMU="${QEMU:-qemu-system-i386}"
QEMU_BIOS_DIR="${QEMU_BIOS_DIR:-/home/ngocnt/opt/share/qemu}"

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

marker_count() {
  clean_serial
  grep -Fxc "$1" "$SERIAL_CLEAN" || true
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
  preempt_test=false
  preempt_a=false
  preempt_b=false
  preempt_ok=false
  preempt_fail=false
  marker_present "BOOT_OK" && boot_ok=true
  marker_present "KERNEL_INIT_OK" && kernel_ok=true
  marker_present "PREEMPT_TEST" && preempt_test=true
  marker_present "PREEMPT_A" && preempt_a=true
  marker_present "PREEMPT_B" && preempt_b=true
  marker_present "PREEMPT_OK" && preempt_ok=true
  marker_present "PREEMPT_FAIL" && preempt_fail=true

  task_json="$(json_escape "$TASK_NAME")"
  printf '{"run_id":"%s","task":"%s","started_at":"%s","ended_at":"%s","qemu_status":%s,"artifacts":[{"path":"build/boot.bin","bytes":"%s","sha256":"%s"},{"path":"build/kernel.bin","bytes":"%s","sha256":"%s"}],"serial_log_sha256":"%s","markers":{"BOOT_OK":%s,"KERNEL_INIT_OK":%s},"subsystem":{"PREEMPT_TEST":%s,"PREEMPT_A":%s,"PREEMPT_B":%s,"PREEMPT_OK":%s,"PREEMPT_FAIL":%s},"verdict":"%s"}\n' \
    "$run_id" "$task_json" "$started_at" "$ended_at" "$qemu_status" \
    "$boot_bytes" "$boot_sha" "$kernel_bytes" "$kernel_sha" "$serial_sha" \
    "$boot_ok" "$kernel_ok" "$preempt_test" "$preempt_a" "$preempt_b" "$preempt_ok" "$preempt_fail" "$verdict" >> "$EVIDENCE_LOG"
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

clean_serial

echo "--- Serial Output ---"
cat "$SERIAL_CLEAN"
echo "--- End Serial Output ---"
echo ""

for marker in BOOT_OK KERNEL_INIT_OK PREEMPT_TEST; do
  if ! marker_present "$marker"; then
    write_evidence "$run_id" "$started_at" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$qemu_status" "fail"
    fail "missing $marker"
  fi
  echo "[PASS] $marker"
done

if marker_present "PREEMPT_FAIL"; then
  write_evidence "$run_id" "$started_at" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$qemu_status" "fail"
  fail "timer preemption selftest reported PREEMPT_FAIL"
fi

if ! marker_present "PREEMPT_A" || ! marker_present "PREEMPT_B" || ! marker_present "PREEMPT_OK"; then
  write_evidence "$run_id" "$started_at" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$qemu_status" "fail"
  fail "preemption not verified - missing PREEMPT_A, PREEMPT_B, or PREEMPT_OK"
fi

preempt_a_count="$(marker_count "PREEMPT_A")"
preempt_b_count="$(marker_count "PREEMPT_B")"
echo "[PASS] non-yielding task A ran ($preempt_a_count markers)"
echo "[PASS] non-yielding task B ran ($preempt_b_count markers)"
echo "[PASS] IRQ0 context switch reached PREEMPT_OK"

ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "pass"
echo "=== TIMER PREEMPTION TEST PASSED ==="
exit 0
