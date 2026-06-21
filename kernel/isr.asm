[bits 32]
section .text

extern isr_handler
extern keyboard_handler
extern timer_handler
extern timer_interrupt
extern syscall_dispatch
extern process_complete_context_switch
extern exception_handler
extern page_fault_dispatch

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

global isr_stub_14
isr_stub_14:
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

    ; Remove the CPU error-code slot while preserving a canonical frame.
    mov eax, [esp + 48]
    mov edx, [esp + 52]
    mov [esp + 48], edx
    mov edx, [esp + 56]
    mov [esp + 52], edx
    test dl, 3
    mov edx, [esp + 60]
    mov [esp + 56], edx
    jz .frame_ready
    mov edx, [esp + 64]
    mov [esp + 60], edx
    mov edx, [esp + 68]
    mov [esp + 64], edx

.frame_ready:
    mov ebp, eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov edx, esp
    push ebp
    push edx
    call page_fault_dispatch
    add esp, 8

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
