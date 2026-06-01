# Build Commands — Cross-Compile, Link, Image Creation

## Toolchain

### Cross-Compiler (BẮT BUỘC)

Dùng `i686-elf-gcc`, KHÔNG dùng `gcc` thường hoặc `-m32` từ host compiler.

| Tool | Purpose | Ghi chú |
|---|---|---|
| `i686-elf-gcc` | Compile C -> i386 object | Freestanding target, không phụ thuộc libc host |
| `i686-elf-gcc` | Link kernel objects -> ELF binary | Prefer compiler driver for final link so `-lgcc` is available |
| `i686-elf-ld` | Low-level linker | Optional/debug use; do not make object-order assumptions |
| `i686-elf-objcopy` | Convert ELF -> raw binary | Bootloader đơn giản load raw bytes, không parse ELF |
| `nasm` | Assemble `.asm` | `boot.asm` dùng `-f bin`; kernel entry dùng `-f elf32` |
| `qemu-system-i386` | Run guest OS | Automated test capture COM1 serial |

### Build cross-compiler (nếu chưa có)

Toolchain bootstrap is a trusted-supply-chain step, not a routine autonomous-agent step. Require human approval, pin versions in a manifest, and verify GNU signatures or SHA256 checksums before building. Record `i686-elf-gcc --version`, `i686-elf-objcopy --version`, and `nasm -v` in evidence.

```bash
# Prerequisites
sudo apt-get install build-essential bison flex libgmp-dev libmpfr-dev libmpc-dev texinfo

# Download sources
export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

# Build binutils
cd /tmp
curl -O https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
tar xf binutils-2.42.tar.xz
mkdir build-binutils
cd build-binutils
../binutils-2.42/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
make
make install

# Build GCC
cd /tmp
curl -O https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
tar xf gcc-13.2.0.tar.xz
mkdir build-gcc
cd build-gcc
../gcc-13.2.0/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c --without-headers
make all-gcc
make install-gcc
```

## Public Artifact Contract

| Artifact | Type | Created by | Used by |
|---|---|---|---|
| `build/boot.bin` | 512-byte flat boot sector | `nasm -f bin` | Sector 0 of `os.img` |
| `build/boot_config.inc` | Generated NASM constants | Makefile from `kernel.bin` size | Included by `boot.asm` |
| `build/entry.o` | ELF32 object | `nasm -f elf32` | Kernel linker |
| `build/*.o` | ELF32 objects | `i686-elf-gcc` / NASM | Kernel linker |
| `build/kernel.elf` | Linked ELF executable | `i686-elf-gcc` final link | Debugging + objcopy input |
| `build/kernel.bin` | Flat kernel image | `i686-elf-objcopy -O binary` | Sector 1+ of `os.img` |
| `build/os.img` | Raw disk image | `dd` | QEMU boot disk |
| `build/serial.log` | Captured COM1 output | `scripts/boot_test.sh` | Automated marker checks |
| `build/qemu.log` | QEMU stdout/stderr | `scripts/boot_test.sh` | Emulator diagnostics, not marker evidence |

## Makefile

```makefile
# Makefile for x86 bare metal OS

# Toolchain
CC = i686-elf-gcc
OBJCOPY = i686-elf-objcopy
NASM = nasm
QEMU = qemu-system-i386

# Directories
BUILD_DIR = build
BOOT_DIR = boot
KERNEL_DIR = kernel

# Flags
CFLAGS = -ffreestanding -fno-builtin -fno-stack-protector \
         -fno-pic -fno-pie -Wall -Wextra -Iinclude -MMD -MP -c
LDFLAGS = -T linker.ld -ffreestanding -nostdlib -Wl,--build-id=none
LIBS = -lgcc
KERNEL_MAX_CHS_SECTORS = 120

# Artifacts
BOOT_BIN = $(BUILD_DIR)/boot.bin
BOOT_CONFIG = $(BUILD_DIR)/boot_config.inc
KERNEL_ENTRY_OBJ = $(BUILD_DIR)/entry.o
KERNEL_OBJ = $(BUILD_DIR)/kernel.o
VGA_OBJ = $(BUILD_DIR)/vga.o
SERIAL_OBJ = $(BUILD_DIR)/serial.o
STRING_OBJ = $(BUILD_DIR)/string.o
KERNEL_OBJECTS = $(KERNEL_ENTRY_OBJ) $(KERNEL_OBJ) $(VGA_OBJ) $(SERIAL_OBJ) $(STRING_OBJ)
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
OS_IMG = $(BUILD_DIR)/os.img

all: guard-paths $(OS_IMG)

guard-paths:
	@test "$(BUILD_DIR)" = "build" || { echo "ERROR: BUILD_DIR must be build"; exit 1; }
	@test "$(OS_IMG)" = "build/os.img" || { echo "ERROR: OS_IMG must be build/os.img"; exit 1; }
	@test -n "$(BUILD_DIR)" || { echo "ERROR: BUILD_DIR is empty"; exit 1; }
	@test -n "$(OS_IMG)" || { echo "ERROR: OS_IMG is empty"; exit 1; }
	@test "$(KERNEL_MAX_CHS_SECTORS)" = "120" || { echo "ERROR: phase-1 track-rolling CHS profile requires KERNEL_MAX_CHS_SECTORS=120"; exit 1; }

$(BOOT_CONFIG): guard-paths $(KERNEL_BIN)
	@mkdir -p $(BUILD_DIR)
	@sectors=$$((($$(wc -c < $(KERNEL_BIN)) + 511) / 512)); \
	if [ "$$sectors" -lt 1 ]; then sectors=1; fi; \
	if [ "$$sectors" -gt "$(KERNEL_MAX_CHS_SECTORS)" ]; then \
		echo "ERROR: kernel.bin requires $$sectors sectors; CHS track-rolling loader supports max $(KERNEL_MAX_CHS_SECTORS)"; \
		echo "Switch to LBA INT 13h AH=42h or a 2-stage loader before growing beyond this profile."; \
		exit 1; \
	fi; \
	printf 'KERNEL_SECTORS equ %s\n' "$$sectors" > $@

$(BOOT_BIN): $(BOOT_DIR)/boot.asm $(BOOT_CONFIG)
	@mkdir -p $(BUILD_DIR)
	$(NASM) -I$(BUILD_DIR)/ -f bin $< -o $@
	@size=$$(wc -c < $@); \
	if [ "$$size" -ne 512 ]; then \
		echo "ERROR: boot.bin must be exactly 512 bytes, got $$size"; \
		echo "If first-stage logic is too large, split into a 2-stage bootloader."; \
		exit 1; \
	fi

$(KERNEL_ENTRY_OBJ): $(KERNEL_DIR)/entry.asm
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(KERNEL_OBJ): $(KERNEL_DIR)/kernel.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(VGA_OBJ): $(KERNEL_DIR)/vga.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(SERIAL_OBJ): $(KERNEL_DIR)/serial.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(STRING_OBJ): $(KERNEL_DIR)/string.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJECTS) linker.ld
	$(CC) $(LDFLAGS) $(KERNEL_OBJECTS) $(LIBS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(OS_IMG): guard-paths $(BOOT_BIN) $(KERNEL_BIN)
	dd if=/dev/zero of="$(OS_IMG)" bs=512 count=2880 2>/dev/null
	dd if="$(BOOT_BIN)" of="$(OS_IMG)" bs=512 count=1 conv=notrunc 2>/dev/null
	dd if="$(KERNEL_BIN)" of="$(OS_IMG)" bs=512 seek=1 conv=notrunc 2>/dev/null

run: $(OS_IMG)
	$(QEMU) -drive file=$<,format=raw -m 512M -no-reboot

run-serial: $(OS_IMG)
	$(QEMU) -drive file=$<,format=raw -m 512M -serial mon:stdio -display none -no-reboot

test: $(OS_IMG)
	@bash scripts/boot_test.sh

clean: guard-paths
	rm -rf $(BUILD_DIR)

-include $(BUILD_DIR)/*.d

.PHONY: all guard-paths run run-serial test clean
```

## Linker Script (`linker.ld`)

```ld
/* Linker script for x86 bare metal kernel */

ENTRY(_start)

SECTIONS
{
    . = 0x1000;  /* Kernel loaded at 0x1000 by boot sector */

    .text BLOCK(4K) :
    {
        KEEP(*(.entry))
        *(.text .text.*)
    }

    .rodata BLOCK(4K) :
    {
        *(.rodata .rodata.*)
    }

    .data BLOCK(4K) :
    {
        *(.data .data.*)
    }

    .bss BLOCK(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.note*)
        *(.comment*)
    }
}
```

## Build Flow chi tiết

```
1. nasm -f elf32 kernel/entry.asm -o build/entry.o
   -> Tạo ELF32 kernel entry point (_start -> call kernel_main).

2. i686-elf-gcc ... -c kernel/kernel.c -o build/kernel.o
   -> Compile C freestanding object.

3. i686-elf-gcc ... -c kernel/vga.c -o build/vga.o
   -> Compile VGA driver.

4. i686-elf-gcc ... -c kernel/serial.c -o build/serial.o
   -> Compile COM1 serial driver.

5. i686-elf-gcc -T linker.ld -ffreestanding -nostdlib build/entry.o build/kernel.o build/vga.o build/serial.o build/string.o -lgcc -o build/kernel.elf
   -> Link kernel ELF at 0x1000. Boot sector is NOT linked into this artifact.

6. i686-elf-objcopy -O binary build/kernel.elf build/kernel.bin
   -> Convert linked ELF into flat raw kernel image.

7. Generate build/boot_config.inc from build/kernel.bin size
   -> `KERNEL_SECTORS equ <ceil(kernel_size / 512)>`.
   -> For the track-rolling CHS loader, fail if this exceeds 120 sectors.

8. nasm -Ibuild/ -f bin boot/boot.asm -o build/boot.bin
   -> Tạo flat boot sector 512 bytes, kết thúc bằng signature 0xAA55.

9. Verify build/boot.bin is exactly 512 bytes.
   -> If NASM reports `TIMES value negative`, split into 2-stage bootloader.

10. dd if=build/boot.bin of=build/os.img bs=512 count=1 conv=notrunc
   -> Write boot sector to sector 0.

11. dd if=build/kernel.bin of=build/os.img bs=512 seek=1 conv=notrunc
   -> Write raw kernel to sector 1+.

12. qemu-system-i386 -drive file=build/os.img,format=raw -serial mon:stdio -display none -no-reboot
    -> Human debug mode: COM1 serial and monitor multiplexed to stdout. Automated tests use a dedicated serial file.
```

## Critical Invariants

- `boot.asm` is a flat binary, never an ELF object for the disk image.
- `boot.bin` is not linked into `kernel.elf`.
- `kernel.elf` is a debug/link artifact; `kernel.bin` is the raw boot artifact.
- `_start` must live in `.entry`, and linker script must `KEEP(*(.entry))` before other text sections.
- `boot_config.inc` is generated from `kernel.bin`; do not silently hardcode stale sector counts.
- The current phase-1 CHS bootloader rolls across floppy tracks and must keep `KERNEL_SECTORS <= 120`; larger kernels require LBA or a 2-stage loader.
- `boot.bin` must be exactly 512 bytes. If first-stage code exceeds the limit, use a 2-stage bootloader.
- If headers include `<stdint.h>`, do not use `-nostdinc` unless the project provides its own `include/stdint.h`.
- Automated tests read COM1 serial output; VGA text does not appear in `build/serial.log`.
- `BUILD_DIR` and `OS_IMG` are guarded before `dd` or `clean`; do not allow command-line overrides to point outside `build/`.

## Common Compile Errors

| Error | Nguyên nhân | Fix |
|---|---|---|
| QEMU says no bootable device | Boot sector is not flat 512-byte binary or missing 0xAA55 | Build with `nasm -f bin`; verify last bytes are `55 aa` |
| Kernel jumps into garbage | Bootloader loaded ELF headers instead of raw code | Use `objcopy -O binary kernel.elf kernel.bin` |
| Kernel crashes after growing | Bootloader reads too few sectors | Generate `boot_config.inc` from `kernel.bin` size |
| Kernel exceeds 120 sectors | Phase-1 CHS loader profile too small | Switch to LBA or a 2-stage loader |
| NASM says `TIMES value negative` | First-stage boot sector exceeds 512 bytes | Split into 2-stage bootloader |
| `undefined reference to 'kernel_main'` | Chưa extern trong `entry.asm` hoặc chưa link `kernel.o` | Add `[extern kernel_main]` and include `kernel.o` |
| `undefined reference to '__stack_chk_fail'` | Stack protector enabled | Add `-fno-stack-protector` |
| `undefined reference to 'memcpy'` / `memset` | Freestanding kernel missing compiler-emitted memory helpers | Add `kernel/string.c` with `memcpy`, `memset`, `memmove`, `memcmp` |
| `undefined reference to '__udivdi3'` | GCC emitted libgcc helper but direct `ld` link bypassed libgcc | Link final ELF with `i686-elf-gcc ... -lgcc` |
| `<stdint.h>` not found | `-nostdinc` used without local headers | Remove `-nostdinc` or add local `include/stdint.h` + `-Iinclude` |
| Serial log empty | Marker printed to VGA/BIOS instead of COM1 | Use COM1 port `0x3F8` for test markers |

## Tài liệu tham khảo

- [QEMU System Emulation](https://www.qemu.org/docs/master/system/)
- [NASM Documentation](https://www.nasm.us/doc/)
- [GNU Binutils objcopy](https://sourceware.org/binutils/docs/binutils/objcopy.html)
- [OSDev Wiki - Boot Sequence](https://wiki.osdev.org/Boot_Sequence)
- [OSDev Wiki - GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler)
