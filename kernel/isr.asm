[bits 32]
section .text

extern isr_handler
extern keyboard_handler
extern timer_handler
extern syscall_handler

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
    push ebx
    push ecx
    push edx

    push edi
    push esi
    push edx
    push ecx
    push eax
    call syscall_handler
    add esp, 20

    pop edx
    pop ecx
    pop ebx
    pop ebp
    iretd
