#!/bin/bash
set -e

OS_IMG="build/os.img"
SERIAL_LOG="build/serial.log"
SERIAL_CLEAN="build/serial.clean.log"
QEMU_LOG="build/qemu.log"
TIMEOUT=30

if [ ! -f "$OS_IMG" ]; then
    echo "ERROR: $OS_IMG not found. Run 'make all' first."
    exit 1
fi

mkdir -p build
: > "$SERIAL_LOG"
: > "$SERIAL_CLEAN"
: > "$QEMU_LOG"

echo "=== Boot Test ==="
echo "Image: $OS_IMG"
echo "Timeout: ${TIMEOUT}s"
echo ""

set +e
timeout "$TIMEOUT" qemu-system-i386 \
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
    0|124)
        ;;
    *)
        echo "[FAIL] qemu exited with status $qemu_status"
        echo "--- QEMU Log ---"
        cat "$QEMU_LOG"
        exit 1
        ;;
esac

tr -d '\r' < "$SERIAL_LOG" > "$SERIAL_CLEAN"

echo "--- Serial Output ---"
cat "$SERIAL_CLEAN"
echo "--- End Serial Output ---"
echo ""

PASS=true

for marker in BOOT_OK KERNEL_INIT_OK; do
    if grep -Fxq "$marker" "$SERIAL_CLEAN"; then
        echo "[PASS] $marker found"
    else
        echo "[FAIL] $marker not found"
        PASS=false
    fi
done

if grep -Fxq "KERNEL_PANIC" "$SERIAL_CLEAN"; then
    echo "[FAIL] KERNEL_PANIC found"
    PASS=false
fi

if grep -Fxq "BOOT_DISK_ERROR" "$SERIAL_CLEAN"; then
    echo "[FAIL] BOOT_DISK_ERROR found"
    PASS=false
fi

if [ "$PASS" = true ]; then
    echo ""
    echo "=== BOOT TEST PASSED ==="
    exit 0
fi

echo ""
echo "=== BOOT TEST FAILED ==="
exit 1
