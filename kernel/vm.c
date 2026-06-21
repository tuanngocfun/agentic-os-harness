#include "vm.h"
#include "frame.h"
#include "paging.h"
#include "process.h"
#include "string.h"
#include <stdint.h>

static uint32_t page_align_down(uint32_t value) {
    return value & ~(VM_PAGE_SIZE - 1);
}

static int is_stack_guard_fault(uint32_t fault_addr) {
    return fault_addr >= USER_STACK_GUARD_BOTTOM &&
           fault_addr < USER_STACK_BOTTOM;
}

static enum vm_fault_result demand_map_heap_page(struct process *proc,
                                                  uint32_t page) {
    uint32_t phys = frame_alloc();

    if (!phys) {
        return VM_FAULT_OOM;
    }
    if (!paging_map_page_in_directory(proc->cr3, page, phys,
                                      PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) {
        frame_release(phys);
        return VM_FAULT_OOM;
    }

    memset((void *)page, 0, VM_PAGE_SIZE);
    return VM_FAULT_HANDLED;
}

enum vm_fault_result vm_handle_page_fault(struct process *proc,
                                          uint32_t fault_addr,
                                          uint32_t error_code) {
    uint32_t page;

    if (!proc || !proc->cr3 || (error_code & VM_FAULT_USER) == 0) {
        return VM_FAULT_FATAL;
    }

    page = page_align_down(fault_addr);
    if ((error_code & (VM_FAULT_PRESENT | VM_FAULT_WRITE)) ==
        (VM_FAULT_PRESENT | VM_FAULT_WRITE)) {
        if (paging_resolve_cow_fault(proc->cr3, page)) {
            return VM_FAULT_HANDLED;
        }
        if (paging_get_page_flags_in_directory(proc->cr3, page) & PAGE_COW) {
            return VM_FAULT_OOM;
        }
        return VM_FAULT_FATAL;
    }

    if ((error_code & VM_FAULT_PRESENT) == 0) {
        if (is_stack_guard_fault(fault_addr)) {
            return VM_FAULT_GUARD;
        }
        if (fault_addr >= proc->heap_start && fault_addr < proc->heap_end) {
            return demand_map_heap_page(proc, page);
        }
    }

    return VM_FAULT_FATAL;
}
