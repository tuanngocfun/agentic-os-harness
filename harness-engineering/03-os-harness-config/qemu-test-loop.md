# QEMU Test Loop — Automated Boot Verification

## Mục đích

Chạy OS trên QEMU, capture COM1 serial output, verify boot markers. Đây là **sensor** chính trong harness.

## QEMU Safety trên Ubuntu Server

```
Ubuntu Server (host, chạy liên tục)
└── qemu-system-i386 (unprivileged userspace process)
    └── build/os.img (guest OS chạy trong QEMU)
```

QEMU là tương đối an toàn khi chạy bằng user thường với image file riêng. Nó không sửa host bootloader/kernel nếu bạn không passthrough disk/device thật.

**Quy tắc an toàn:**
- Chạy QEMU bằng user thường, không dùng root.
- Chỉ dùng image file riêng như `build/os.img`.
- Không dùng `-drive file=/dev/sdX`.
- Không passthrough hardware thật khi chưa có lý do rõ ràng.
- Không mount shared folder host ở chế độ writeable trong giai đoạn boot harness.
- Không dùng network bridge/TAP không kiểm soát cho boot tests cơ bản.

## QEMU Command

### Graphic mode (debug)

```bash
qemu-system-i386 \
  -drive file=build/os.img,format=raw \
  -m 512M \
  -no-reboot
```

### Serial/Headless mode (automated testing)

```bash
qemu-system-i386 \
  -drive file=build/os.img,format=raw \
  -m 512M \
  -serial file:build/serial.log \
  -monitor none \
  -nic none \
  -display none \
  -no-reboot \
  > build/qemu.log 2>&1
```

`-serial mon:stdio` vẫn hữu ích cho human debug (`make run-serial`) vì nó đưa COM1 ra terminal, nhưng automated tests không dùng stream này làm evidence vì nó multiplex COM1 với QEMU monitor.

| Flag | Ý nghĩa |
|---|---|
| `-drive file=build/os.img,format=raw` | Raw disk image created by Makefile |
| `-m 512M` | 512MB RAM cho guest |
| `-serial file:build/serial.log` | COM1 serial ghi vào evidence file riêng |
| `-monitor none` | Disable QEMU monitor trong automated test |
| `-nic none` | Disable networking trong boot smoke test |
| `-display none` | Không mở GUI; VGA/BIOS text không phải serial log |
| `-no-reboot` | Dừng thay vì reboot liên tục khi triple fault |

## Boot Test Script (`scripts/boot_test.sh`)

```bash
#!/bin/bash
# Automated boot test for x86 bare metal OS
# Exit code: 0 = PASS, 1 = FAIL

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
```

## Integration với Makefile

```makefile
test: $(OS_IMG)
	@bash scripts/boot_test.sh

run-serial: $(OS_IMG)
	$(QEMU) -drive file=$<,format=raw -m 512M -serial mon:stdio -display none -no-reboot
```

## Expected Output

### Boot thành công tối thiểu

```
=== Boot Test ===
Image: build/os.img
Timeout: 30s

--- Serial Output ---
BOOT_OK
KERNEL_INIT_OK
--- End Serial Output ---

[PASS] BOOT_OK found
[PASS] KERNEL_INIT_OK found

=== BOOT TEST PASSED ===
```

### Boot thành công khi đã có shell

```
BOOT_OK
KERNEL_INIT_OK
SHELL_READY
```

`SHELL_READY` là optional marker. Test cơ bản không fail nếu marker này chưa có.

### Boot thất bại

```
--- Serial Output ---
BOOT_OK
--- End Serial Output ---

[PASS] BOOT_OK found
[FAIL] KERNEL_INIT_OK not found

=== BOOT TEST FAILED ===
```

## Troubleshooting

| Triệu chứng | Nguyên nhân có thể | Debug |
|---|---|---|
| Timeout, không output | Boot sector không chạy hoặc không init COM1 | Check `boot.bin` flat 512 bytes, 0xAA55, serial init |
| Chỉ có `BOOT_OK` | Kernel không load/jump đúng | Check sector count, `kernel.bin`, GDT, stack |
| QEMU exit ngay | Triple fault | Run graphic mode, inspect GDT and protected-mode transition |
| `KERNEL_PANIC` | Kernel panic handler triggered | Read panic message after marker |
| VGA có text nhưng serial log empty | Code prints to VGA/BIOS only | Print markers to COM1 |

## Tài liệu tham khảo

- [QEMU Documentation](https://www.qemu.org/docs/master/system/qemu-manpage.html)
- [QEMU System Emulation](https://www.qemu.org/docs/master/system/)
