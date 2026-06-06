[bits 32]
section .entry
[extern kernel_main]
[extern __bss_start]
[extern __bss_end]

global _start
_start:
    mov ebp, 0x90000
    mov esp, ebp

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    call kernel_main

    cli
    hlt
