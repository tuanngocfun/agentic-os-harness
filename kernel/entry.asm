[bits 32]
section .entry
[extern kernel_main]

global _start
_start:
    mov ebp, 0x90000
    mov esp, ebp

    call kernel_main

    cli
    hlt
