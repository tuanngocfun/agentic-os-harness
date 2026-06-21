#ifndef VM_H
#define VM_H

#include <stdint.h>

struct process;

#define VM_PAGE_SIZE 4096u
#define VM_FAULT_PRESENT 0x01u
#define VM_FAULT_WRITE   0x02u
#define VM_FAULT_USER    0x04u

#define VM_EXIT_PAGE_FAULT 0x8000000Eu
#define VM_EXIT_GUARD_PAGE 0x8000001Eu
#define VM_EXIT_OOM        0x8000002Eu

enum vm_fault_result {
    VM_FAULT_FATAL = 0,
    VM_FAULT_HANDLED = 1,
    VM_FAULT_GUARD = 2,
    VM_FAULT_OOM = 3
};

enum vm_fault_result vm_handle_page_fault(struct process *proc,
                                          uint32_t fault_addr,
                                          uint32_t error_code);

#endif
