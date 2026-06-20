[bits 32]
section .text

extern isr_handler
extern keyboard_handler
extern timer_handler
extern timer_interrupt
extern syscall_dispatch
extern process_complete_context_switch
extern exception_handler

%macro ISR_NOERRCODE 1
global isr_stub_%1
isr_stub_%1:
    pusha
    push dword 0
    push dword %1
    call exception_handler
    add esp, 8
    popa
    iretd
%endmacro

%macro ISR_ERRCODE 1
global isr_stub_%1
isr_stub_%1:
    pusha
    mov eax, [esp + 32]
    push eax
    push dword %1
    call exception_handler
    add esp, 8
    popa
    add esp, 4
    iretd
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 6
ISR_ERRCODE 13
ISR_ERRCODE 14

global isr_stub_32
isr_stub_32:
    sub esp, 4
    mov word [esp], ds
    mov word [esp + 2], 0
    sub esp, 4
    mov word [esp], es
    mov word [esp + 2], 0
    sub esp, 4
    mov word [esp], fs
    mov word [esp + 2], 0
    sub esp, 4
    mov word [esp], gs
    mov word [esp + 2], 0
    pusha

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call timer_interrupt
    add esp, 4

    mov esp, eax
    call process_complete_context_switch
    mov al, 0x20
    out 0x20, al

    popa
    push eax

    mov ax, [esp + 4]
    mov gs, ax
    mov ax, [esp + 8]
    mov fs, ax
    mov ax, [esp + 12]
    mov es, ax
    mov ax, [esp + 16]
    mov ds, ax

    pop eax
    add esp, 16
    iretd

global isr_stub_33
isr_stub_33:
    pusha
    call keyboard_handler
    push dword 33
    call isr_handler
    add esp, 4
    popa
    iretd

global isr_stub_128
isr_stub_128:
    sub esp, 4
    mov word [esp], ds
    mov word [esp + 2], 0
    sub esp, 4
    mov word [esp], es
    mov word [esp + 2], 0
    sub esp, 4
    mov word [esp], fs
    mov word [esp + 2], 0
    sub esp, 4
    mov word [esp], gs
    mov word [esp + 2], 0
    pusha

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call syscall_dispatch
    add esp, 4

    test eax, eax
    jz .no_runnable_process
    mov esp, eax
    call process_complete_context_switch

    popa
    push eax

    mov ax, [esp + 4]
    mov gs, ax
    mov ax, [esp + 8]
    mov fs, ax
    mov ax, [esp + 12]
    mov es, ax
    mov ax, [esp + 16]
    mov ds, ax

    pop eax
    add esp, 16
    iretd

.no_runnable_process:
    cli
.halt:
    hlt
    jmp .halt
