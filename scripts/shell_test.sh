#!/usr/bin/env bash
set -u

PATH="${TOOLCHAIN_PATH:-/home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin}:$PATH"

BUILD_DIR="${BUILD_DIR:-build}"
OS_IMG="${OS_IMG:-build/os.img}"
SERIAL_LOG="$BUILD_DIR/serial.shell.log"
SERIAL_CLEAN="$BUILD_DIR/serial.shell.clean.log"
QEMU_LOG="$BUILD_DIR/qemu.shell.log"
MONITOR_LOG="$BUILD_DIR/qemu.shell.monitor.log"
VGA_DUMP="$BUILD_DIR/vga.shell.bin"
VGA_TEXT="$BUILD_DIR/vga.shell.txt"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-12}"
TASK_NAME="${TASK_NAME:-shell-runtime-test}"
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

qemu_alive() {
  kill -0 "$qemu_pid" 2>/dev/null
}

monitor_cmd() {
  qemu_alive || fail "qemu exited during $TASK_NAME; see $QEMU_LOG and $MONITOR_LOG"
  printf '%s\n' "$1" >&"${QEMU_MON[1]}" || fail "failed to write monitor command: $1"
  sleep 0.15
}

send_keys() {
  for key in "$@"; do
    monitor_cmd "sendkey $key 1"
    sleep 0.10
  done
}

send_line() {
  send_keys "$@" ret
  sleep 0.80
}

dump_vga_text() {
  dump_path="$1"
  text_path="$2"
  monitor_cmd "pmemsave 0xb8000 4000 $dump_path"
  [ -f "$dump_path" ] || fail "missing VGA dump: $dump_path"

  perl -e '
    open my $fh, "<:raw", $ARGV[0] or die "open $ARGV[0]: $!";
    local $/;
    my $d = <$fh>;
    for my $row (0..24) {
      my $s = "";
      for my $col (0..79) {
        my $i = ($row * 80 + $col) * 2;
        my $c = substr($d, $i, 1);
        my $o = ord($c);
        $s .= ($o >= 32 && $o < 127) ? $c : " ";
      }
      $s =~ s/\s+$//;
      print "$s\n";
    }
  ' "$dump_path" > "$text_path" || fail "failed to decode VGA text"
}

[ "$BUILD_DIR" = "build" ] || fail "BUILD_DIR must be build"
[ "$OS_IMG" = "build/os.img" ] || fail "OS_IMG must be build/os.img"
[ -f "$OS_IMG" ] || fail "$OS_IMG not found; run make all first"
command -v "$QEMU" >/dev/null 2>&1 || fail "$QEMU not found in PATH"
command -v perl >/dev/null 2>&1 || fail "perl not found in PATH"

mkdir -p "$BUILD_DIR"
: > "$SERIAL_LOG"
: > "$SERIAL_CLEAN"
: > "$QEMU_LOG"
: > "$MONITOR_LOG"
: > "$VGA_TEXT"

coproc QEMU_MON {
  "$QEMU" \
    -drive file="$OS_IMG",format=raw \
    -m 512M \
    -serial file:"$SERIAL_LOG" \
    -monitor stdio \
    -nic none \
    -display none \
    -no-reboot \
    > "$MONITOR_LOG" 2> "$QEMU_LOG"
}
qemu_pid="$QEMU_MON_PID"

cleanup() {
  if qemu_alive; then
    printf 'quit\n' >&"${QEMU_MON[1]}" 2>/dev/null || true
    sleep 0.2
  fi
  if qemu_alive; then
    kill "$qemu_pid" 2>/dev/null || true
  fi
  wait "$qemu_pid" 2>/dev/null || true
}
trap cleanup EXIT

deadline=$((SECONDS + TIMEOUT_SECONDS))
while [ "$SECONDS" -lt "$deadline" ]; do
  if marker_present "SHELL_READY"; then
    break
  fi
  qemu_alive || fail "qemu exited before SHELL_READY; see $QEMU_LOG and $MONITOR_LOG"
  sleep 0.2
done

marker_present "BOOT_OK" || fail "missing BOOT_OK"
marker_present "KERNEL_INIT_OK" || fail "missing KERNEL_INIT_OK"
marker_present "SHELL_READY" || fail "missing SHELL_READY"
failure_present "KERNEL_PANIC" && fail "found KERNEL_PANIC"
failure_present "BOOT_DISK_ERROR" && fail "found BOOT_DISK_ERROR"

send_line h e l p
dump_vga_text "$VGA_DUMP" "$VGA_TEXT"

grep -Fq "Shell ready. Type 'help' for commands." "$VGA_TEXT" || fail "VGA shell banner missing"
grep -Fq "Available commands:" "$VGA_TEXT" || fail "help command did not render"

echo "[PASS] BOOT_OK"
echo "[PASS] KERNEL_INIT_OK"
echo "[PASS] SHELL_READY"
echo "[PASS] help command rendered"
echo "=== SHELL RUNTIME TEST PASSED ==="
