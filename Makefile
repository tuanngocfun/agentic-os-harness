# Makefile for x86 bare metal OS

export PATH := /home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin:$(PATH)

CC = i686-elf-gcc
OBJCOPY = i686-elf-objcopy
NASM = nasm
QEMU = qemu-system-i386

BUILD_DIR = build
BOOT_DIR = boot
KERNEL_DIR = kernel

CFLAGS = -ffreestanding -fno-builtin -fno-stack-protector \
         -fno-pic -fno-pie -Wall -Wextra -Iinclude -MMD -MP -c
LDFLAGS = -T linker.ld -ffreestanding -nostdlib -Wl,--build-id=none
LIBS = -lgcc
KERNEL_MAX_CHS_SECTORS = 17

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
	@test "$(KERNEL_MAX_CHS_SECTORS)" = "17" || { echo "ERROR: KERNEL_MAX_CHS_SECTORS must be 17"; exit 1; }

$(BOOT_CONFIG): guard-paths $(KERNEL_BIN)
	@mkdir -p $(BUILD_DIR)
	@sectors=$$((($$(wc -c < $(KERNEL_BIN)) + 511) / 512)); \
	if [ "$$sectors" -lt 1 ]; then sectors=1; fi; \
	if [ "$$sectors" -gt "$(KERNEL_MAX_CHS_SECTORS)" ]; then \
		echo "ERROR: kernel.bin requires $$sectors sectors; CHS single-read supports max $(KERNEL_MAX_CHS_SECTORS)"; \
		exit 1; \
	fi; \
	printf 'KERNEL_SECTORS equ %s\n' "$$sectors" > $@

$(BOOT_BIN): $(BOOT_DIR)/boot.asm $(BOOT_CONFIG)
	@mkdir -p $(BUILD_DIR)
	$(NASM) -I$(BUILD_DIR)/ -f bin $< -o $@
	@size=$$(wc -c < $@); \
	if [ "$$size" -ne 512 ]; then \
		echo "ERROR: boot.bin must be exactly 512 bytes, got $$size"; \
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
