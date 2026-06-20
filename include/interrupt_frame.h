#ifndef INTERRUPT_FRAME_H
#define INTERRUPT_FRAME_H

#include <stdint.h>

/*
 * Canonical stack image shared by timer and syscall interrupt entry.
 * The first eight fields match the memory order produced by PUSHAD.
 * user_esp and user_ss are present when the interrupted context is ring 3.
 */
struct interrupt_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t saved_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;
    uint32_t user_ss;
};

#endif
