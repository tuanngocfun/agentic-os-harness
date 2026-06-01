[bits 16]
[org 0x7C00]

KERNEL_OFFSET equ 0x1000
COM1 equ 0x3F8

%include "boot_config.inc"

%if KERNEL_SECTORS > 120
%error "CHS track-rolling loader supports at most 120 kernel sectors"
%endif

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

load_kernel:
    mov si, KERNEL_OFFSET
    mov cx, KERNEL_SECTORS
    mov byte [CUR_CYLINDER], 0
    mov byte [CUR_HEAD], 0
    mov byte [CUR_SECTOR], 2

.loop:
    test cx, cx
    jz .done

    push cx
    push si

    mov ah, 0x02
    mov al, 1
    mov ch, [CUR_CYLINDER]
    mov cl, [CUR_SECTOR]
    mov dh, [CUR_HEAD]
    mov dl, [BOOT_DRIVE]
    mov bx, si
    int 0x13
    jc disk_error

    pop si
    pop cx

    add si, 512
    inc byte [CUR_SECTOR]
    cmp byte [CUR_SECTOR], 18
    jbe .next
    mov byte [CUR_SECTOR], 1
    inc byte [CUR_HEAD]
    cmp byte [CUR_HEAD], 1
    jbe .next
    mov byte [CUR_HEAD], 0
    inc byte [CUR_CYLINDER]

.next:
    dec cx
    jmp .loop

.done:
    ret

disk_error:
    mov si, MSG_BOOT_DISK_ERROR
    call serial_puts_16
    jmp $

switch_to_pm:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:init_pm

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    mov esi, MSG_BOOT_OK
    call serial_puts_32

    call KERNEL_OFFSET
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

BOOT_DRIVE: db 0
CUR_CYLINDER: db 0
CUR_HEAD: db 0
CUR_SECTOR: db 2
MSG_BOOT_OK: db 'BOOT_OK', 0x0A, 0
MSG_BOOT_DISK_ERROR: db 'BOOT_DISK_ERROR', 0x0A, 0

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
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

times 510-($-$$) db 0
dw 0xAA55
