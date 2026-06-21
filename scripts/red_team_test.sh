#!/usr/bin/env bash
set -u

# Red/blue adversarial regression gate. Passing means the current known
# attacks were attempted, blocked, and recorded. It is still not a broad
# security certification.

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.red_team.log"
SERIAL_CLEAN="$BUILD_DIR/serial.red_team.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.red_team.log"
FINDINGS_DIR="$BUILD_DIR/red-team"
FINDINGS_LOG="$FINDINGS_DIR/findings.jsonl"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-10}"
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

write_findings() {
  mkdir -p "$FINDINGS_DIR"
  : > "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-HARNESS-001","subsystem":"syscall-test-harness","severity":"high","status":"blocked_by_blue_team","evidence_marker":"RED_MARKER_FORGERY_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-harness-001"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-HARNESS-003","subsystem":"syscall-test-marker-authorization","severity":"high","status":"blocked_by_blue_team","evidence_marker":"RED_MARKER_REPLAY_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-harness-003"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-EXEC-001","subsystem":"process-exec","severity":"high","status":"blocked_by_blue_team","evidence_marker":"RED_EXEC_RESIDUAL_MAPPING_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-exec-001"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-FS-001","subsystem":"simplefs","severity":"medium","status":"blocked_by_blue_team","evidence_marker":"RED_SIMPLEFS_DOS_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-fs-001"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-FS-002","subsystem":"simplefs-namespace","severity":"medium","status":"blocked_by_blue_team","evidence_marker":"RED_VFS_NAMESPACE_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-fs-002"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-EXEC-002","subsystem":"process-exec-fd-table","severity":"high","status":"blocked_by_blue_team","evidence_marker":"RED_EXEC_FD_LEAK_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-exec-002"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-ELF-001","subsystem":"elf-loader","severity":"high","status":"blocked_by_blue_team","evidence_marker":"RED_ELF_OVERLAP_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-elf-001"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-EXEC-003","subsystem":"process-exec-failure-cleanup","severity":"medium","status":"blocked_by_blue_team","evidence_marker":"RED_EXEC_FAILURE_CLEANUP_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-exec-003"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-PROC-001","subsystem":"process-lifecycle-cleanup","severity":"medium","status":"blocked_by_blue_team","evidence_marker":"RED_PROCESS_DESTROY_CLEANUP_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-proc-001"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-PROC-002","subsystem":"process-fork-failure-cleanup","severity":"high","status":"blocked_by_blue_team","evidence_marker":"RED_FORK_FAILURE_CLEANUP_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-proc-002"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-SYSCALL-001","subsystem":"syscall-test-only-surface","severity":"medium","status":"blocked_by_blue_team","evidence_marker":"RED_SYSCALL_PRIVILEGE_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-syscall-001"}' >> "$FINDINGS_LOG"
  printf '%s\n' '{"id":"RT-SCHED-001","subsystem":"scheduler-preemption-boundary","severity":"medium","status":"blocked_by_blue_team","evidence_marker":"RED_SCHED_YIELD_MIXING_BLOCKED","patch_playbook":"harness-engineering/blue-team/PATCH_PLAYBOOKS.md#rt-sched-001"}' >> "$FINDINGS_LOG"
}

echo "=== Red-Team / Blue-Team Defense Gate ==="

[ "$BUILD_DIR" = "build" ] || fail "BUILD_DIR must be build"
[ "$OS_IMG" = "build/os.img" ] || fail "$OS_IMG must be build/os.img"
[ -f "$OS_IMG" ] || fail "$OS_IMG not found; run make all first"
command -v "$QEMU" >/dev/null 2>&1 || fail "$QEMU not found in PATH"

mkdir -p "$BUILD_DIR" "$FINDINGS_DIR"
: > "$SERIAL_LOG"
: > "$SERIAL_CLEAN"
: > "$QEMU_LOG"
: > "$FINDINGS_LOG"

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

marker_present "BOOT_OK" || fail "missing BOOT_OK"
marker_present "KERNEL_INIT_OK" || fail "missing KERNEL_INIT_OK"
failure_present "KERNEL_PANIC" && fail "found KERNEL_PANIC"
failure_present "RAMDISK_INIT_FATAL" && fail "ramdisk fatal init failure"
failure_present "VFS_INIT_FATAL" && fail "VFS fatal init failure"
failure_present "RED_TEAM_FAIL" && fail "red-team selftest reported RED_TEAM_FAIL"

marker_present "RED_TEAM_TEST" || fail "red-team test did not start"
marker_present "SYSCALL_FILE_OK" && fail "marker forgery emitted a trusted non-red marker"
marker_present "RED_MARKER_FORGERY_BLOCKED" || fail "RT-HARNESS-001 attack was not blocked"
marker_present "RED_MARKER_REPLAY_BLOCKED" || fail "RT-HARNESS-003 attack was not blocked"
marker_present "RED_SYSCALL_PRIVILEGE_BLOCKED" || fail "RT-SYSCALL-001 attack was not blocked"
marker_present "RED_EXEC_RESIDUAL_MAPPING_BLOCKED" || fail "RT-EXEC-001 attack was not blocked"
marker_present "RED_SCHED_YIELD_MIXING_BLOCKED" || fail "RT-SCHED-001 attack was not blocked"
marker_present "RED_SIMPLEFS_DOS_BLOCKED" || fail "RT-FS-001 attack was not blocked"
marker_present "RED_VFS_NAMESPACE_BLOCKED" || fail "RT-FS-002 attack was not blocked"
marker_present "RED_ELF_OVERLAP_BLOCKED" || fail "RT-ELF-001 attack was not blocked"
marker_present "RED_EXEC_FAILURE_CLEANUP_BLOCKED" || fail "RT-EXEC-003 attack was not blocked"
marker_present "RED_PROCESS_DESTROY_CLEANUP_BLOCKED" || fail "RT-PROC-001 attack was not blocked"
marker_present "RED_FORK_FAILURE_CLEANUP_BLOCKED" || fail "RT-PROC-002 attack was not blocked"
marker_present "RED_EXEC_FD_LEAK_BLOCKED" || fail "RT-EXEC-002 attack was not blocked"
marker_present "RED_DEFENSES_OK" || fail "red/blue defense gate incomplete"

write_findings

[ -s "$FINDINGS_LOG" ] || fail "missing red-team findings JSONL"
grep -Fq '"id":"RT-HARNESS-001"' "$FINDINGS_LOG" || fail "missing RT-HARNESS-001 finding"
grep -Fq '"id":"RT-HARNESS-003"' "$FINDINGS_LOG" || fail "missing RT-HARNESS-003 finding"
grep -Fq '"id":"RT-EXEC-001"' "$FINDINGS_LOG" || fail "missing RT-EXEC-001 finding"
grep -Fq '"id":"RT-FS-001"' "$FINDINGS_LOG" || fail "missing RT-FS-001 finding"
grep -Fq '"id":"RT-FS-002"' "$FINDINGS_LOG" || fail "missing RT-FS-002 finding"
grep -Fq '"id":"RT-EXEC-002"' "$FINDINGS_LOG" || fail "missing RT-EXEC-002 finding"
grep -Fq '"id":"RT-ELF-001"' "$FINDINGS_LOG" || fail "missing RT-ELF-001 finding"
grep -Fq '"id":"RT-EXEC-003"' "$FINDINGS_LOG" || fail "missing RT-EXEC-003 finding"
grep -Fq '"id":"RT-PROC-001"' "$FINDINGS_LOG" || fail "missing RT-PROC-001 finding"
grep -Fq '"id":"RT-PROC-002"' "$FINDINGS_LOG" || fail "missing RT-PROC-002 finding"
grep -Fq '"id":"RT-SYSCALL-001"' "$FINDINGS_LOG" || fail "missing RT-SYSCALL-001 finding"
grep -Fq '"id":"RT-SCHED-001"' "$FINDINGS_LOG" || fail "missing RT-SCHED-001 finding"

echo "[PASS] RT-HARNESS-001 marker forgery blocked"
echo "[PASS] RT-HARNESS-003 retired-token and replay attacks blocked"
echo "[PASS] RT-SYSCALL-001 test-only syscall surface blocked"
echo "[PASS] RT-EXEC-001 exec residual mapping blocked"
echo "[PASS] RT-SCHED-001 preemptive yield mixing blocked"
echo "[PASS] RT-FS-001 SimpleFS exhaustion blocked"
echo "[PASS] RT-FS-002 namespace abuse blocked"
echo "[PASS] RT-ELF-001 overlapping ELF segments blocked"
echo "[PASS] RT-EXEC-003 failed exec cleanup blocked"
echo "[PASS] RT-PROC-001 process destroy cleanup blocked"
echo "[PASS] RT-PROC-002 failed fork cleanup blocked"
echo "[PASS] RT-EXEC-002 exec FD leak blocked"
echo "[PASS] findings written to $FINDINGS_LOG"
echo ""
echo "=== RED/BLUE DEFENSE GATE PASSED ==="
