[bits 32]
section .text

global switch_to_usermode
switch_to_usermode:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, esp
    push dword 0x23
    push eax
    push dword 0x202
    push dword 0x1B
    push dword .user_entry
    iretd

.user_entry:
    ret

global context_switch
context_switch:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov eax, [ebp + 8]
    mov [eax], esp

    mov eax, [ebp + 12]
    mov esp, [eax]

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
