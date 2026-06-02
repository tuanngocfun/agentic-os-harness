[bits 32]
section .text

extern isr_handler
extern keyboard_handler
extern timer_handler
extern syscall_handler
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
    pusha
    call timer_handler
    push dword 32
    call isr_handler
    add esp, 4
    popa
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
    push ebp
    mov ebp, esp
    sub esp, 4

    push ebx
    push ecx
    push edx
    push esi
    push edi

    push edx
    push ecx
    push ebx
    push eax
    call syscall_handler
    add esp, 16

    mov [ebp - 4], eax

    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx

    mov eax, [ebp - 4]
    mov esp, ebp
    pop ebp
    iretd
