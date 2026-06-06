; E820 Memory Map Structure
; Each entry is 24 bytes:
;   uint64_t base_addr
;   uint64_t length
;   uint32_t type (1=usable, 2=reserved, etc.)
;   uint32_t extended_attributes

; Reference-only helper. The active boot sector keeps its compact E820 routine
; inline in boot.asm to stay within 512 bytes.
; Memory map handoff is stored at physical 0x80000 as segment 0x8000:0.
E820_MAP_SEG equ 0x8000
E820_MAP_OFF equ 0x0000
E820_MAX_ENTRIES equ 32

detect_e820:
    pusha
    push es
    mov ax, E820_MAP_SEG
    mov es, ax
    mov di, E820_MAP_OFF
    xor ebx, ebx                ; EBX = 0 on first call
    xor bp, bp                  ; BP = entry count

.e820_loop:
    mov eax, 0xE820             ; E820 function
    mov ecx, 24                 ; Buffer size
    mov edx, 0x534D4150         ; 'SMAP' signature
    int 0x15

    jc .e820_failed             ; CF=1 means error or done
    mov edx, 0x534D4150         ; Restore EDX (some BIOSes trash it)
    cmp eax, edx                ; EAX should be 'SMAP' on success
    jne .e820_failed

    test ebx, ebx               ; EBX = 0 means last entry
    je .e820_done

    add di, 24                  ; Next entry
    inc bp                      ; Increment count
    cmp bp, E820_MAX_ENTRIES    ; Check if we've hit max
    jge .e820_done
    jmp .e820_loop

.e820_failed:
    ; E820 not supported or failed
    ; Write entry count = 0
    mov word [es:E820_MAP_OFF], 0
    pop es
    popa
    ret

.e820_done:
    ; Store entry count at start of map
    mov [es:E820_MAP_OFF], bp
    pop es
    popa
    ret
