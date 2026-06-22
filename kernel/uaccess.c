#include "uaccess.h"
#include "paging.h"
#include "process.h"
#include "string.h"
#include "syscall.h"
#include "vm.h"
#include <stdint.h>

#define PAGE_MASK 0xFFFFF000u
#define PAGE_SIZE 0x1000u

int uaccess_range_valid(uint32_t address, uint32_t size) {
    uint32_t end;

    if (size == 0) {
        return address == 0 ||
               (address >= USER_SPACE_START && address < USER_SPACE_END);
    }
    if (address < USER_SPACE_START || address >= USER_SPACE_END) {
        return 0;
    }
    end = address + size;
    if (end < address || end > USER_SPACE_END) {
        return 0;
    }
    return 1;
}

static int active_process_directory(struct process **out) {
    struct process *proc = process_get_current();

    if (!proc || !proc->cr3 ||
        proc->cr3 != paging_get_current_directory()) {
        return 0;
    }
    *out = proc;
    return 1;
}

int uaccess_readable(uint32_t address, uint32_t size) {
    struct process *proc;
    uint32_t first;
    uint32_t last;

    if (!uaccess_range_valid(address, size)) {
        return 0;
    }
    if (size == 0) {
        return 1;
    }
    if (!active_process_directory(&proc)) {
        return 0;
    }

    first = address & PAGE_MASK;
    last = (address + size - 1) & PAGE_MASK;
    for (uint32_t page = first;; page += PAGE_SIZE) {
        uint32_t flags = paging_get_page_flags_in_directory(proc->cr3, page);
        if ((flags & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) {
            return 0;
        }
        if (page == last) {
            break;
        }
    }
    return 1;
}

int uaccess_prepare_write(uint32_t address, uint32_t size) {
    struct process *proc;
    uint32_t first;
    uint32_t last;

    if (!uaccess_range_valid(address, size)) {
        return UACCESS_EFAULT;
    }
    if (size == 0) {
        return UACCESS_SUCCESS;
    }
    if (!active_process_directory(&proc)) {
        return UACCESS_EFAULT;
    }

    first = address & PAGE_MASK;
    last = (address + size - 1) & PAGE_MASK;
    for (uint32_t page = first;; page += PAGE_SIZE) {
        enum vm_fault_result result = vm_prepare_user_write_page(proc, page);
        if (result == VM_FAULT_OOM) {
            return UACCESS_ENOMEM;
        }
        if (result != VM_FAULT_HANDLED) {
            return UACCESS_EFAULT;
        }
        if (page == last) {
            break;
        }
    }
    return UACCESS_SUCCESS;
}

int copy_from_user(void *dest, uint32_t source, uint32_t size) {
    if ((!dest && size > 0) || !uaccess_readable(source, size)) {
        return UACCESS_EFAULT;
    }
    if (size > 0) {
        memcpy(dest, (const void *)source, size);
    }
    return UACCESS_SUCCESS;
}

int copy_to_user(uint32_t dest, const void *source, uint32_t size) {
    int status;

    if (!source && size > 0) {
        return UACCESS_EFAULT;
    }
    status = uaccess_prepare_write(dest, size);
    if (status != UACCESS_SUCCESS) {
        return status;
    }
    if (size > 0) {
        memcpy((void *)dest, source, size);
    }
    return UACCESS_SUCCESS;
}

int copy_string_from_user(char *dest, uint32_t source, uint32_t max_size) {
    if (!dest || max_size == 0) {
        return UACCESS_EFAULT;
    }

    for (uint32_t i = 0; i < max_size; i++) {
        if (copy_from_user(&dest[i], source + i, 1) != UACCESS_SUCCESS) {
            return UACCESS_EFAULT;
        }
        if (dest[i] == '\0') {
            return UACCESS_SUCCESS;
        }
    }
    dest[max_size - 1] = '\0';
    return UACCESS_EFAULT;
}
