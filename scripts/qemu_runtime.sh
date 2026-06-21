#!/usr/bin/env bash

# Shared fail-closed QEMU launch contract. Test scripts must explicitly provide
# QEMU_BIOS_DIR; this helper never discovers or substitutes another directory.

QEMU_RUNTIME_EXPECTED_VERSION="QEMU emulator version 4.2.1"
QEMU_RUNTIME_BIOS_SHA256="9ecd1852625a19430006fa1b2b94a7ae49e459e7044eae17db602572a987b95c"
QEMU_RUNTIME_KVMVAPIC_SHA256="cdf057a71b07e3b52b19cbe210bdefa59250d01a9810b960f7fe1f98eed95a27"
QEMU_RUNTIME_VGABIOS_SHA256="e7e2881a477678dd7daad694dc98e61109d87b74a91443d0cf082802a8991d6a"
QEMU_RUNTIME_BIOS_SIZE=262144
QEMU_RUNTIME_KVMVAPIC_SIZE=9216
QEMU_RUNTIME_VGABIOS_SIZE=39424

qemu_runtime_fail() {
  echo "[FAIL] QEMU runtime contract: $*" >&2
  return 1
}

qemu_runtime_json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

qemu_runtime_check_bios_file() {
  file="$1"
  expected_size="$2"
  expected_sha="$3"

  [ -e "$file" ] || qemu_runtime_fail "missing BIOS file: $file" || return 1
  [ ! -L "$file" ] || qemu_runtime_fail "BIOS file must not be a symlink: $file" || return 1
  [ -f "$file" ] || qemu_runtime_fail "BIOS path is not a regular file: $file" || return 1
  [ -r "$file" ] || qemu_runtime_fail "BIOS file is not readable: $file" || return 1

  actual_size="$(stat -c %s "$file")" || return 1
  [ "$actual_size" = "$expected_size" ] ||
    qemu_runtime_fail "BIOS size mismatch for $file" || return 1

  actual_sha="$(sha256sum "$file" | awk '{print $1}')" || return 1
  [ "$actual_sha" = "$expected_sha" ] ||
    qemu_runtime_fail "BIOS SHA-256 mismatch for $file" || return 1
}

qemu_runtime_preflight() {
  : "${QEMU:=qemu-system-i386}"
  : "${BUILD_DIR:=build}"
  : "${TASK_NAME:=$(basename "$0" .sh)}"

  [ -n "${QEMU_BIOS_DIR:-}" ] ||
    qemu_runtime_fail "QEMU_BIOS_DIR must be explicitly set" || return 1
  case "$QEMU_BIOS_DIR" in
    /*) ;;
    *) qemu_runtime_fail "QEMU_BIOS_DIR must be an absolute path" || return 1 ;;
  esac
  [ -d "$QEMU_BIOS_DIR" ] ||
    qemu_runtime_fail "QEMU_BIOS_DIR is not a directory: $QEMU_BIOS_DIR" || return 1

  qemu_runtime_check_bios_file "$QEMU_BIOS_DIR/bios-256k.bin" "$QEMU_RUNTIME_BIOS_SIZE" "$QEMU_RUNTIME_BIOS_SHA256" || return 1
  qemu_runtime_check_bios_file "$QEMU_BIOS_DIR/kvmvapic.bin" "$QEMU_RUNTIME_KVMVAPIC_SIZE" "$QEMU_RUNTIME_KVMVAPIC_SHA256" || return 1
  qemu_runtime_check_bios_file "$QEMU_BIOS_DIR/vgabios-stdvga.bin" "$QEMU_RUNTIME_VGABIOS_SIZE" "$QEMU_RUNTIME_VGABIOS_SHA256" || return 1

  QEMU_RUNTIME_QEMU_PATH="$(command -v "$QEMU")" ||
    qemu_runtime_fail "QEMU executable not found: $QEMU" || return 1
  [ -f "$QEMU_RUNTIME_QEMU_PATH" ] ||
    qemu_runtime_fail "QEMU executable is not a regular file" || return 1
  [ -x "$QEMU_RUNTIME_QEMU_PATH" ] ||
    qemu_runtime_fail "QEMU executable is not executable" || return 1

  QEMU_RUNTIME_QEMU_VERSION="$("$QEMU_RUNTIME_QEMU_PATH" --version | sed -n '1p')" ||
    return 1
  [ "$QEMU_RUNTIME_QEMU_VERSION" = "$QEMU_RUNTIME_EXPECTED_VERSION" ] ||
    qemu_runtime_fail "unexpected QEMU version: $QEMU_RUNTIME_QEMU_VERSION" || return 1

  [ -n "${OS_IMG:-}" ] && [ -f "$OS_IMG" ] ||
    qemu_runtime_fail "OS image is missing: ${OS_IMG:-unset}" || return 1

  QEMU_RUNTIME_EVIDENCE_LOG="${QEMU_RUNTIME_EVIDENCE_LOG:-$BUILD_DIR/qemu-runtime.jsonl}"
  QEMU_RUNTIME_PREFLIGHT_OK=1
}

qemu_runtime_begin() {
  [ "${QEMU_RUNTIME_PREFLIGHT_OK:-0}" = 1 ] ||
    qemu_runtime_fail "preflight was not completed" || return 1
  [ -n "${SERIAL_LOG:-}" ] && [ -n "${QEMU_LOG:-}" ] ||
    qemu_runtime_fail "SERIAL_LOG and QEMU_LOG must be set" || return 1

  mkdir -p "$BUILD_DIR"
  : > "$SERIAL_LOG"
  : > "$QEMU_LOG"
  if [ -n "${SERIAL_CLEAN:-}" ]; then
    : > "$SERIAL_CLEAN"
  fi

  QEMU_RUNTIME_RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)-$$"
  QEMU_RUNTIME_STARTED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  QEMU_RUNTIME_STARTED_EPOCH="$(date +%s)"
  QEMU_RUNTIME_RECORDED=0
}

qemu_runtime_require_exact_marker() {
  marker="$1"
  [ -s "$SERIAL_LOG" ] ||
    qemu_runtime_fail "serial log is empty" || return 1
  tr -d '\r' < "$SERIAL_LOG" | grep -Fxq "$marker" ||
    qemu_runtime_fail "missing exact marker: $marker" || return 1
}

qemu_runtime_record() {
  qemu_status="$1"
  completion_mode="$2"

  [ "${QEMU_RUNTIME_RECORDED:-0}" = 0 ] || return 0

  ended_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  image_sha="$(sha256sum "$OS_IMG" | awk '{print $1}')"
  serial_sha="missing"
  serial_nonempty=false
  serial_fresh=false
  if [ -f "$SERIAL_LOG" ]; then
    serial_sha="$(sha256sum "$SERIAL_LOG" | awk '{print $1}')"
    [ -s "$SERIAL_LOG" ] && serial_nonempty=true
    serial_mtime="$(stat -c %Y "$SERIAL_LOG")"
    [ "$serial_mtime" -ge "$QEMU_RUNTIME_STARTED_EPOCH" ] && serial_fresh=true
  fi

  task_json="$(qemu_runtime_json_escape "$TASK_NAME")"
  qemu_json="$(qemu_runtime_json_escape "$QEMU_RUNTIME_QEMU_PATH")"
  version_json="$(qemu_runtime_json_escape "$QEMU_RUNTIME_QEMU_VERSION")"
  bios_json="$(qemu_runtime_json_escape "$QEMU_BIOS_DIR")"

  printf '{"run_id":"%s","task":"%s","started_at":"%s","ended_at":"%s","completion_mode":"%s","qemu_status":%s,"qemu_path":"%s","qemu_version":"%s","bios_dir":"%s","bios_sha256":"%s","kvmvapic_sha256":"%s","vgabios_sha256":"%s","os_image":"%s","os_image_sha256":"%s","serial_log":"%s","serial_log_sha256":"%s","serial_nonempty":%s,"serial_fresh":%s,"runtime_executed":true}\n' "$QEMU_RUNTIME_RUN_ID" "$task_json" "$QEMU_RUNTIME_STARTED_AT" "$ended_at" "$completion_mode" "$qemu_status" "$qemu_json" "$version_json" "$bios_json" "$QEMU_RUNTIME_BIOS_SHA256" "$QEMU_RUNTIME_KVMVAPIC_SHA256" "$QEMU_RUNTIME_VGABIOS_SHA256" "$OS_IMG" "$image_sha" "$SERIAL_LOG" "$serial_sha" "$serial_nonempty" "$serial_fresh" >> "$QEMU_RUNTIME_EVIDENCE_LOG"

  QEMU_RUNTIME_RECORDED=1
}

qemu_runtime_validate() {
  qemu_status="$1"
  completion_mode="$2"

  case "$completion_mode" in
    timeout)
      [ "$qemu_status" -eq 124 ] ||
        qemu_runtime_fail "timeout-driven QEMU exited with status $qemu_status" || return 1
      ;;
    interactive)
      [ "$qemu_status" -eq 0 ] ||
        qemu_runtime_fail "interactive QEMU exited with status $qemu_status" || return 1
      ;;
    *)
      qemu_runtime_fail "unknown completion mode: $completion_mode" || return 1
      ;;
  esac

  [ -s "$SERIAL_LOG" ] ||
    qemu_runtime_fail "serial log is empty after QEMU execution" || return 1
  serial_mtime="$(stat -c %Y "$SERIAL_LOG")" || return 1
  [ "$serial_mtime" -ge "$QEMU_RUNTIME_STARTED_EPOCH" ] ||
    qemu_runtime_fail "serial log predates this QEMU run" || return 1

  echo "QEMU_RUNTIME_EXECUTED_OK"
}

qemu_runtime_verify() {
  qemu_status="$1"
  completion_mode="$2"

  qemu_runtime_record "$qemu_status" "$completion_mode" || return 1
  qemu_runtime_validate "$qemu_status" "$completion_mode"
}
