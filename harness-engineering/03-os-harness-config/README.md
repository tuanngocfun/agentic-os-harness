# 03 — OS Harness Configuration

## Tổng quan

Section này chứa cấu hình harness **cụ thể** cho dự án OS x86 bare metal viết bằng C. Đây là nơi lý thuyết từ `00-overview/` và `02-core-harness/` trở thành **thực tế**.

## Stack kỹ thuật

```
Host: Ubuntu Server
├── Cross-compiler: i686-elf-gcc final link, i686-elf-objcopy
├── Assembler: NASM (Netwide Assembler)
├── Build: GNU Make
├── Emulator: QEMU (qemu-system-i386)
└── Target: x86 bare metal, 32-bit protected mode
```

## QEMU Safety trên Ubuntu Server

QEMU chạy như **userspace process** trên Ubuntu host:
- Guest OS chạy bên trong QEMU, không động tới host bootloader/kernel nếu không passthrough disk/device thật
- Không cần tắt server, không cần reboot
- Sử dụng image file riêng, không passthrough device thật
- Chạy QEMU bằng user thường, không cần root

## Các file trong section này

| File | Nội dung |
|---|---|
| `README.md` | Bạn đang ở đây |
| `agents-md-os-template.md` | AGENTS.md sẵn sàng dùng cho OS project |
| `os-boot-sequence.md` | Chi tiết boot chain: BIOS → boot sector → protected mode → kernel |
| `build-commands.md` | Cross-compile, link, image creation, QEMU run |
| `qemu-test-loop.md` | Automated boot test với serial capture |
| `boot-markers.md` | Protocol boot markers: core required `BOOT_OK`, `KERNEL_INIT_OK`; current shell phase also requires `SHELL_READY`; optional `TESTS_PASS` |

## Mục tiêu cuối cùng

> Agent đọc AGENTS.md -> dùng skills -> build `boot.bin`, `boot_config.inc`, `kernel.elf`, `kernel.bin`, `os.img` -> QEMU boot test dùng dedicated serial file -> parse exact required marker lines (`BOOT_OK`, `KERNEL_INIT_OK`, and current-phase `SHELL_READY`) -> shell-runtime test proves `help` -> PASS
