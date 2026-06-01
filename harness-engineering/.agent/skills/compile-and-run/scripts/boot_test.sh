#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.log"
SERIAL_CLEAN="$BUILD_DIR/serial.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.log"
EVIDENCE_LOG="$BUILD_DIR/evidence.jsonl"
LOCK_DIR="$BUILD_DIR/.boot_test.lock"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-30}"
TASK_NAME="${TASK_NAME:-boot-test}"
QEMU="${QEMU:-qemu-system-i386}"
ALLOW_QEMU_EXIT="${ALLOW_QEMU_EXIT:-0}"

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

marker_present() {
  grep -Fxq "$1" "$SERIAL_CLEAN"
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
  sectors="unknown"

  [ -f "$BUILD_DIR/boot.bin" ] && boot_bytes="$(file_bytes "$BUILD_DIR/boot.bin")" && boot_sha="$(file_sha256 "$BUILD_DIR/boot.bin")"
  [ -f "$BUILD_DIR/kernel.bin" ] && kernel_bytes="$(file_bytes "$BUILD_DIR/kernel.bin")" && kernel_sha="$(file_sha256 "$BUILD_DIR/kernel.bin")"
  [ -f "$SERIAL_LOG" ] && serial_sha="$(file_sha256 "$SERIAL_LOG")"
  [ -f "$BUILD_DIR/boot_config.inc" ] && sectors="$(awk '/KERNEL_SECTORS/ {print $3}' "$BUILD_DIR/boot_config.inc")"

  boot_ok=false
  kernel_ok=false
  shell_ready=false
  boot_disk_error=false
  kernel_panic=false
  marker_present "BOOT_OK" && boot_ok=true
  marker_present "KERNEL_INIT_OK" && kernel_ok=true
  marker_present "SHELL_READY" && shell_ready=true
  marker_present "BOOT_DISK_ERROR" && boot_disk_error=true
  marker_present "KERNEL_PANIC" && kernel_panic=true

  task_json="$(json_escape "$TASK_NAME")"
  printf '{"run_id":"%s","task":"%s","started_at":"%s","ended_at":"%s","qemu_status":%s,"artifacts":[{"path":"build/boot.bin","bytes":"%s","sha256":"%s"},{"path":"build/kernel.bin","bytes":"%s","sha256":"%s"}],"serial_log_sha256":"%s","sector_count":{"kernel_sectors":"%s","phase1_chs_limit":120},"markers":{"BOOT_OK":%s,"KERNEL_INIT_OK":%s,"SHELL_READY":%s,"BOOT_DISK_ERROR":%s,"KERNEL_PANIC":%s},"safety":{"os_img":"%s","monitor_none":true,"nic_none":true,"serial_file":true},"verdict":"%s"}\n' \
    "$run_id" "$task_json" "$started_at" "$ended_at" "$qemu_status" \
    "$boot_bytes" "$boot_sha" "$kernel_bytes" "$kernel_sha" "$serial_sha" "$sectors" \
    "$boot_ok" "$kernel_ok" "$shell_ready" "$boot_disk_error" "$kernel_panic" "$OS_IMG" "$verdict" >> "$EVIDENCE_LOG"
}

[ "$BUILD_DIR" = "build" ] || fail "BUILD_DIR must be build"
[ "$OS_IMG" = "build/os.img" ] || fail "OS_IMG must be build/os.img"
[ -f "$OS_IMG" ] || fail "$OS_IMG not found; run make all first"
command -v "$QEMU" >/dev/null 2>&1 || fail "$QEMU not found in PATH"

mkdir -p "$BUILD_DIR"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  fail "boot test lock exists: $LOCK_DIR"
fi
trap 'rmdir "$LOCK_DIR" 2>/dev/null || true' EXIT

: > "$SERIAL_LOG"
: > "$SERIAL_CLEAN"
: > "$QEMU_LOG"

run_id="$(date -u +%Y%m%dT%H%M%SZ)-$$"
started_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

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

tr -d '\r' < "$SERIAL_LOG" > "$SERIAL_CLEAN"

case "$qemu_status" in
  124) ;;
  0)
    if [ "$ALLOW_QEMU_EXIT" != "1" ]; then
      ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
      write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "fail"
      fail "qemu exited before timeout; this can indicate shutdown or triple fault after markers"
    fi
    ;;
  *)
    ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "fail"
    fail "qemu exited with status $qemu_status; see $QEMU_LOG"
    ;;
esac

pass=true
for marker in BOOT_OK KERNEL_INIT_OK SHELL_READY; do
  if marker_present "$marker"; then
    echo "[PASS] $marker"
  else
    echo "[FAIL] missing $marker"
    pass=false
  fi
done

for marker in BOOT_DISK_ERROR KERNEL_PANIC; do
  if marker_present "$marker"; then
    echo "[FAIL] found $marker"
    pass=false
  fi
done

ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
if [ "$pass" = true ]; then
  write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "pass"
  echo "=== BOOT TEST PASSED ==="
  exit 0
fi

write_evidence "$run_id" "$started_at" "$ended_at" "$qemu_status" "fail"
echo "=== BOOT TEST FAILED ==="
exit 1
