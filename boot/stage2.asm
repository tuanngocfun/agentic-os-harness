[bits 16]
[org 0x0000]

STAGE2_PHYS equ 0x90000
KERNEL_OFFSET equ 0x1000
COM1 equ 0x3F8
E820_SEG equ 0x8000
E820_MAGIC equ 0x30323845
E820_MAX_ENTRIES equ 32
E820_HEADER_SIZE equ 8
E820_ENTRY_SIZE equ 24
KERNEL_CHUNK_SECTORS equ 32

%include "boot_config.inc"

stage2_start:
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ax, 0x7000
    mov ss, ax
    xor sp, sp
    sti

    mov [BOOT_DRIVE], dl

    call serial_init_16
    mov si, MSG_STAGE2_OK
    call serial_puts_16

    call detect_drive_geometry
    call detect_e820
    call enable_a20
    call load_kernel
    call switch_to_pm

    jmp $

serial_init_16:
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al
    mov dx, COM1 + 0
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al
    mov dx, COM1 + 4
    mov al, 0x0B
    out dx, al
    ret

serial_putc_16:
    push dx
    push ax
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    pop ax
    mov dx, COM1
    out dx, al
    pop dx
    ret

serial_puts_16:
    lodsb
    test al, al
    jz .done
    call serial_putc_16
    jmp serial_puts_16
.done:
    ret

enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

detect_drive_geometry:
    mov ah, 0x08
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .fallback

    and cl, 0x3F
    jz .fallback
    mov [SECTORS_PER_TRACK], cl
    mov [LAST_HEAD], dh
    jmp .done

.fallback:
    mov byte [SECTORS_PER_TRACK], 18
    mov byte [LAST_HEAD], 1

.done:
    ret

load_kernel:
    mov word [SECTORS_LEFT], KERNEL_SECTORS
    mov dword [CURRENT_LBA], KERNEL_LBA_START
    mov word [DEST_SEG], KERNEL_OFFSET >> 4

.loop:
    cmp word [SECTORS_LEFT], 0
    je .done

    mov ax, [SECTORS_LEFT]
    cmp ax, KERNEL_CHUNK_SECTORS
    jbe .count_ready
    mov ax, KERNEL_CHUNK_SECTORS

.count_ready:
    mov [DAP_COUNT], ax
    mov word [DAP_OFFSET], 0
    mov bx, [DEST_SEG]
    mov [DAP_SEGMENT], bx
    mov eax, [CURRENT_LBA]
    mov [DAP_LBA_LOW], eax
    mov dword [DAP_LBA_HIGH], 0

    mov si, DAP
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error

    xor ecx, ecx
    mov cx, [DAP_COUNT]
    add [CURRENT_LBA], ecx

    mov ax, [DAP_COUNT]
    sub [SECTORS_LEFT], ax
    shl ax, 5
    add [DEST_SEG], ax
    jmp .loop

.done:
    ret

disk_error:
    mov si, MSG_BOOT_DISK_ERROR
    call serial_puts_16
    jmp $

detect_e820:
    push es

    mov ax, E820_SEG
    mov es, ax
    mov dword [es:0], E820_MAGIC
    mov word [es:4], 0

    mov di, E820_HEADER_SIZE
    xor bp, bp
    xor ebx, ebx

.e820_loop:
    mov dword [es:di + 20], 1
    mov eax, 0xE820
    mov ecx, E820_ENTRY_SIZE
    mov edx, 0x534D4150
    int 0x15

    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done

    inc bp
    add di, E820_ENTRY_SIZE
    cmp bp, E820_MAX_ENTRIES
    jge .e820_done

    test ebx, ebx
    jne .e820_loop

.e820_done:
    mov word [es:4], bp
    pop es
    ret

switch_to_pm:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SEG:(STAGE2_PHYS + init_pm)

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x70000
    mov esp, ebp

    mov esi, STAGE2_PHYS + MSG_BOOT_OK
    call serial_puts_32

    mov eax, KERNEL_OFFSET
    call eax
    jmp $

serial_putc_32:
    push edx
    push eax
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    pop eax
    mov dx, COM1
    out dx, al
    pop edx
    ret

serial_puts_32:
    lodsb
    test al, al
    jz .done
    call serial_putc_32
    jmp serial_puts_32
.done:
    ret

[bits 16]
BOOT_DRIVE: db 0
SECTORS_PER_TRACK: db 18
LAST_HEAD: db 1
SECTORS_LEFT: dw 0
DEST_SEG: dw 0
CURRENT_LBA: dd 0

MSG_STAGE2_OK: db 'STAGE2_OK', 0x0A, 0
MSG_BOOT_OK: db 'BOOT_OK', 0x0A, 0
MSG_BOOT_DISK_ERROR: db 'BOOT_DISK_ERROR', 0x0A, 0

DAP:
    db 0x10
    db 0
DAP_COUNT: dw 0
DAP_OFFSET: dw 0
DAP_SEGMENT: dw 0
DAP_LBA_LOW: dd 0
DAP_LBA_HIGH: dd 0

gdt_start:
    dq 0x0000000000000000

gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd STAGE2_PHYS + gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start
