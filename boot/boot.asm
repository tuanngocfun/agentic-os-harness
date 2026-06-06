[bits 16]
[org 0x7C00]

COM1 equ 0x3F8
STAGE2_SEG equ 0x9000

%include "boot_config.inc"

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [BOOT_DRIVE], dl

    call serial_init_16
    call load_stage2

    jmp STAGE2_SEG:0x0000

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

load_stage2:
    mov word [DAP_COUNT], STAGE2_LOAD_SECTORS
    mov word [DAP_OFFSET], 0x0000
    mov word [DAP_SEGMENT], STAGE2_SEG
    mov dword [DAP_LBA_LOW], 1
    mov dword [DAP_LBA_HIGH], 0

    mov si, DAP
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error
    ret

disk_error:
    mov si, MSG_BOOT_DISK_ERROR
    call serial_puts_16
    jmp $

BOOT_DRIVE: db 0
MSG_BOOT_DISK_ERROR: db 'BOOT_DISK_ERROR', 0x0A, 0

DAP:
    db 0x10
    db 0
DAP_COUNT: dw 0
DAP_OFFSET: dw 0
DAP_SEGMENT: dw 0
DAP_LBA_LOW: dd 0
DAP_LBA_HIGH: dd 0

times 510 - ($ - $$) db 0
dw 0xAA55
