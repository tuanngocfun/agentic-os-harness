/* Linker script for x86 bare metal kernel */

ENTRY(_start)

SECTIONS
{
    . = 0x1000;

    .text BLOCK(4K) :
    {
        KEEP(*(.entry))
        *(.text .text.*)
    }

    .rodata BLOCK(4K) :
    {
        *(.rodata .rodata.*)
    }

    .data BLOCK(4K) :
    {
        *(.data .data.*)
    }

    .bss BLOCK(4K) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.note*)
        *(.comment*)
    }
}
