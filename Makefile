# Makefile for x86 bare metal OS

export PATH := /home/ngocnt/opt/cross/bin:/home/ngocnt/opt/bin:$(PATH)

CC = i686-elf-gcc
OBJCOPY = i686-elf-objcopy
NASM = nasm
QEMU = qemu-system-i386

BUILD_DIR = build
BOOT_DIR = boot
KERNEL_DIR = kernel

KERNEL_DEFINES ?=
CFLAGS = -ffreestanding -fno-builtin -fno-stack-protector \
         -fno-pic -fno-pie -Wall -Wextra -Iinclude -MMD -MP $(KERNEL_DEFINES) -c
LDFLAGS = -T linker.ld -ffreestanding -nostdlib -Wl,--build-id=none
LIBS = -lgcc
STAGE2_RESERVED_SECTORS = 32
KERNEL_LBA_START = 33
FLOPPY_SECTORS = 2880

BOOT_BIN = $(BUILD_DIR)/boot.bin
STAGE2_BIN = $(BUILD_DIR)/stage2.bin
BOOT_CONFIG = $(BUILD_DIR)/boot_config.inc
KERNEL_ENTRY_OBJ = $(BUILD_DIR)/entry.o
ISR_OBJ = $(BUILD_DIR)/isr.o
KERNEL_OBJ = $(BUILD_DIR)/kernel.o
VGA_OBJ = $(BUILD_DIR)/vga.o
SERIAL_OBJ = $(BUILD_DIR)/serial.o
STRING_OBJ = $(BUILD_DIR)/string.o
IDT_OBJ = $(BUILD_DIR)/idt.o
KEYBOARD_OBJ = $(BUILD_DIR)/keyboard.o
TIMER_OBJ = $(BUILD_DIR)/timer.o
E820_OBJ = $(BUILD_DIR)/e820.o
MEMORY_OBJ = $(BUILD_DIR)/memory.o
FRAME_OBJ = $(BUILD_DIR)/frame.o
ALLOCATOR_OBJ = $(BUILD_DIR)/allocator.o
RAMDISK_OBJ = $(BUILD_DIR)/ramdisk.o
SIMPLEFS_OBJ = $(BUILD_DIR)/simplefs.o
VFS_OBJ = $(BUILD_DIR)/vfs.o
ELF_OBJ = $(BUILD_DIR)/elf.o
SHELL_OBJ = $(BUILD_DIR)/shell.o
GDT_OBJ = $(BUILD_DIR)/gdt.o
PAGING_OBJ = $(BUILD_DIR)/paging.o
TSS_OBJ = $(BUILD_DIR)/tss.o
SYSCALL_OBJ = $(BUILD_DIR)/syscall.o
PROCESS_OBJ = $(BUILD_DIR)/process.o
SCHEDULER_OBJ = $(BUILD_DIR)/scheduler.o
USERMODE_OBJ = $(BUILD_DIR)/usermode.o
KERNEL_OBJECTS = $(KERNEL_ENTRY_OBJ) $(ISR_OBJ) $(KERNEL_OBJ) $(VGA_OBJ) $(SERIAL_OBJ) $(STRING_OBJ) $(IDT_OBJ) $(KEYBOARD_OBJ) $(TIMER_OBJ) $(E820_OBJ) $(MEMORY_OBJ) $(FRAME_OBJ) $(ALLOCATOR_OBJ) $(RAMDISK_OBJ) $(SIMPLEFS_OBJ) $(VFS_OBJ) $(ELF_OBJ) $(GDT_OBJ) $(PAGING_OBJ) $(TSS_OBJ) $(SYSCALL_OBJ) $(PROCESS_OBJ) $(SCHEDULER_OBJ) $(USERMODE_OBJ) $(SHELL_OBJ)
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
KERNEL_DEFINES_STAMP = $(BUILD_DIR)/kernel_defines.stamp
OS_IMG = $(BUILD_DIR)/os.img

all: guard-paths $(OS_IMG)

guard-paths:
	@test "$(BUILD_DIR)" = "build" || { echo "ERROR: BUILD_DIR must be build"; exit 1; }
	@test "$(OS_IMG)" = "build/os.img" || { echo "ERROR: OS_IMG must be build/os.img"; exit 1; }
	@test -n "$(BUILD_DIR)" || { echo "ERROR: BUILD_DIR is empty"; exit 1; }
	@test -n "$(OS_IMG)" || { echo "ERROR: OS_IMG is empty"; exit 1; }
	@test "$(STAGE2_RESERVED_SECTORS)" = "32" || { echo "ERROR: STAGE2_RESERVED_SECTORS must be 32"; exit 1; }
	@test "$(KERNEL_LBA_START)" = "33" || { echo "ERROR: KERNEL_LBA_START must be 33"; exit 1; }

$(KERNEL_DEFINES_STAMP): guard-paths FORCE
	@mkdir -p $(BUILD_DIR)
	@tmp="$(KERNEL_DEFINES_STAMP).tmp"; \
	printf '%s\n' "$(KERNEL_DEFINES)" > "$$tmp"; \
	if [ ! -f "$@" ] || ! cmp -s "$$tmp" "$@"; then \
		mv "$$tmp" "$@"; \
	else \
		rm "$$tmp"; \
	fi

$(BOOT_CONFIG): guard-paths $(KERNEL_BIN)
	@mkdir -p $(BUILD_DIR)
	@sectors=$$((($$(wc -c < $(KERNEL_BIN)) + 511) / 512)); \
	if [ "$$sectors" -lt 1 ]; then sectors=1; fi; \
	if [ $$(( $(KERNEL_LBA_START) + $$sectors )) -gt "$(FLOPPY_SECTORS)" ]; then \
		echo "ERROR: image requires $$(( $(KERNEL_LBA_START) + $$sectors )) sectors; floppy image supports $(FLOPPY_SECTORS)"; \
		exit 1; \
	fi; \
	printf 'STAGE2_LOAD_SECTORS equ %s\n' "$(STAGE2_RESERVED_SECTORS)" > $@; \
	printf 'KERNEL_LBA_START equ %s\n' "$(KERNEL_LBA_START)" >> $@; \
	printf 'KERNEL_SECTORS equ %s\n' "$$sectors" >> $@

$(BOOT_BIN): $(BOOT_DIR)/boot.asm $(BOOT_CONFIG)
	@mkdir -p $(BUILD_DIR)
	$(NASM) -I$(BUILD_DIR)/ -f bin $< -o $@
	@size=$$(wc -c < $@); \
	if [ "$$size" -ne 512 ]; then \
		echo "ERROR: boot.bin must be exactly 512 bytes, got $$size"; \
		exit 1; \
	fi

$(STAGE2_BIN): $(BOOT_DIR)/stage2.asm $(BOOT_CONFIG)
	@mkdir -p $(BUILD_DIR)
	$(NASM) -I$(BUILD_DIR)/ -f bin $< -o $@
	@size=$$(wc -c < $@); \
	limit=$$(( $(STAGE2_RESERVED_SECTORS) * 512 )); \
	if [ "$$size" -gt "$$limit" ]; then \
		echo "ERROR: stage2.bin must fit in $(STAGE2_RESERVED_SECTORS) sectors ($$limit bytes), got $$size"; \
		exit 1; \
	fi

$(KERNEL_ENTRY_OBJ): $(KERNEL_DIR)/entry.asm
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(ISR_OBJ): $(KERNEL_DIR)/isr.asm
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(KERNEL_OBJ): $(KERNEL_DIR)/kernel.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(VGA_OBJ): $(KERNEL_DIR)/vga.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(SERIAL_OBJ): $(KERNEL_DIR)/serial.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(STRING_OBJ): $(KERNEL_DIR)/string.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(IDT_OBJ): $(KERNEL_DIR)/idt.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(KEYBOARD_OBJ): $(KERNEL_DIR)/keyboard.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(TIMER_OBJ): $(KERNEL_DIR)/timer.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(E820_OBJ): $(KERNEL_DIR)/e820.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(MEMORY_OBJ): $(KERNEL_DIR)/memory.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(FRAME_OBJ): $(KERNEL_DIR)/frame.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(ALLOCATOR_OBJ): $(KERNEL_DIR)/allocator.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(RAMDISK_OBJ): $(KERNEL_DIR)/ramdisk.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(SIMPLEFS_OBJ): $(KERNEL_DIR)/simplefs.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(VFS_OBJ): $(KERNEL_DIR)/vfs.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(ELF_OBJ): $(KERNEL_DIR)/elf.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(GDT_OBJ): $(KERNEL_DIR)/gdt.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(PAGING_OBJ): $(KERNEL_DIR)/paging.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(TSS_OBJ): $(KERNEL_DIR)/tss.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(SYSCALL_OBJ): $(KERNEL_DIR)/syscall.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(PROCESS_OBJ): $(KERNEL_DIR)/process.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(SCHEDULER_OBJ): $(KERNEL_DIR)/scheduler.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(USERMODE_OBJ): $(KERNEL_DIR)/usermode.asm
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(SHELL_OBJ): $(KERNEL_DIR)/shell.c $(KERNEL_DEFINES_STAMP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJECTS) linker.ld
	$(CC) $(LDFLAGS) $(KERNEL_OBJECTS) $(LIBS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(OS_IMG): guard-paths $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	dd if=/dev/zero of="$(OS_IMG)" bs=512 count=$(FLOPPY_SECTORS) 2>/dev/null
	dd if="$(BOOT_BIN)" of="$(OS_IMG)" bs=512 count=1 conv=notrunc 2>/dev/null
	dd if="$(STAGE2_BIN)" of="$(OS_IMG)" bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if="$(KERNEL_BIN)" of="$(OS_IMG)" bs=512 seek=$(KERNEL_LBA_START) conv=notrunc 2>/dev/null

run: $(OS_IMG)
	$(QEMU) -drive file=$<,format=raw -m 512M -no-reboot

run-serial: $(OS_IMG)
	$(QEMU) -drive file=$<,format=raw -m 512M -serial mon:stdio -display none -no-reboot

test: test-boot test-shell

test-deep: test-syscall test-exception test-exception-div0 test-exception-gpf test-exception-pagefault test-scheduler test-paging test-memory test-usermode test-timer test-timer-preemption test-allocator test-address-space test-syscall-negative test-syscall-file test-elf-loader test-process-syscall test-process-lifecycle test-e820-frame test-ramdisk test-vfs test-scheduler-safety test-shell-io

test-boot: $(OS_IMG)
	@bash scripts/boot_test.sh

test-shell: $(OS_IMG)
	@bash scripts/shell_test.sh

test-syscall:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_SYSCALL_ABI_SELFTEST
	@bash scripts/syscall_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-exception:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_EXCEPTION_SELFTEST
	@EXCEPTION_VECTOR=6 bash scripts/exception_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-exception-div0:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_EXCEPTION_DIV0_SELFTEST
	@EXCEPTION_VECTOR=0 bash scripts/exception_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-exception-gpf:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_EXCEPTION_GPF_SELFTEST
	@EXCEPTION_VECTOR=13 bash scripts/exception_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-exception-pagefault:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_EXCEPTION_PAGEFAULT_SELFTEST
	@EXCEPTION_VECTOR=14 bash scripts/exception_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-scheduler:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_SCHEDULER_SELFTEST
	@bash scripts/scheduler_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-paging:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_PAGING_SELFTEST
	@bash scripts/paging_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-memory:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_MEMORY_SELFTEST
	@bash scripts/memory_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-usermode:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_USERMODE_SELFTEST
	@bash scripts/usermode_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-timer:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_TIMER_SELFTEST
	@bash scripts/timer_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-timer-preemption:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_TIMER_PREEMPTION_SELFTEST
	@bash scripts/timer_preemption_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-allocator:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_ALLOCATOR_SELFTEST
	@bash scripts/allocator_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-address-space:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_ADDRESS_SPACE_SELFTEST
	@bash scripts/address_space_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-syscall-negative:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_SYSCALL_NEGATIVE_SELFTEST
	@bash scripts/syscall_negative_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-syscall-file:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_SYSCALL_FILE_SELFTEST
	@bash scripts/syscall_file_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-elf-loader:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_ELF_LOADER_SELFTEST
	@bash scripts/elf_loader_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-process-syscall:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_PROCESS_SYSCALL_SELFTEST
	@bash scripts/process_syscall_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-process-lifecycle:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_PROCESS_LIFECYCLE_SELFTEST
	@bash scripts/process_lifecycle_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-e820-frame:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_E820_SELFTEST
	@bash scripts/e820_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-e820: test-e820-frame

test-ramdisk:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_RAMDISK_SELFTEST
	@bash scripts/ramdisk_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-vfs:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_VFS_SELFTEST
	@bash scripts/vfs_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-scheduler-safety:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_SCHEDULER_SAFETY_SELFTEST
	@bash scripts/scheduler_safety_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-red-team:
	@$(MAKE) -B all KERNEL_DEFINES=-DENABLE_REDTEAM_SELFTEST
	@bash scripts/red_team_test.sh; status=$$?; $(MAKE) -B all; exit $$status

test-blue-team: test-red-team

test-shell-io: $(OS_IMG)
	@bash scripts/shell_io_test.sh

clean: guard-paths
	rm -rf $(BUILD_DIR)

-include $(BUILD_DIR)/*.d

FORCE:

.PHONY: all guard-paths run run-serial test test-deep test-boot test-shell test-syscall test-exception test-exception-div0 test-exception-gpf test-exception-pagefault test-scheduler test-paging test-memory test-usermode test-timer test-timer-preemption test-allocator test-address-space test-syscall-negative test-syscall-file test-elf-loader test-process-syscall test-process-lifecycle test-e820-frame test-e820 test-ramdisk test-vfs test-scheduler-safety test-red-team test-blue-team test-shell-io clean FORCE
