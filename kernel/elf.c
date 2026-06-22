#include "elf.h"
#include "frame.h"
#include "paging.h"
#include "string.h"
#include "vfs.h"
#include <stdint.h>

#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELF_CLASS_32 1
#define ELF_DATA_LSB 1
#define ELF_VERSION_CURRENT 1
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_386 3
#define ELF_PT_LOAD 1
#define ELF_PF_W 0x02u

struct elf32_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf32_program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed));

static uint8_t elf_file_buffer[ELF_MAX_IMAGE_SIZE];

static uint32_t align_down(uint32_t value) {
    return value & ~(FRAME_SIZE - 1);
}

static uint32_t align_up(uint32_t value) {
    return (value + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
}

static int add_overflows(uint32_t a, uint32_t b) {
    return (a + b) < a;
}

static int power_of_two_or_zero(uint32_t value) {
    if (value == 0) {
        return 1;
    }
    return (value & (value - 1)) == 0;
}

static int range_inside_file(uint32_t offset, uint32_t size, uint32_t file_size) {
    if (add_overflows(offset, size)) {
        return 0;
    }
    return (offset + size) <= file_size;
}

static int range_inside_user_space(uint32_t start, uint32_t size) {
    if (size == 0 || add_overflows(start, size)) {
        return 0;
    }
    if (start < ELF_USER_LOAD_MIN) {
        return 0;
    }
    return (start + size) <= ELF_USER_LOAD_MAX;
}

static int read_file_into_buffer(const char *path, uint32_t *size_out) {
    struct vfs_stat stat;
    uint32_t done = 0;
    int fd;

    if (!path || !size_out) {
        return ELF_EINVAL;
    }

    int status = vfs_stat(path, &stat);
    if (status == VFS_ENOENT) {
        return ELF_ENOENT;
    }
    if (status != VFS_SUCCESS) {
        return ELF_EIO;
    }
    if (stat.size == 0 || stat.size > ELF_MAX_IMAGE_SIZE) {
        return ELF_EINVAL;
    }

    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        return fd == VFS_ENOENT ? ELF_ENOENT : ELF_EIO;
    }

    while (done < stat.size) {
        int count = vfs_read(fd, elf_file_buffer + done, stat.size - done);
        if (count <= 0) {
            vfs_close(fd);
            return ELF_EIO;
        }
        done += (uint32_t)count;
    }

    if (vfs_close(fd) != VFS_SUCCESS || done != stat.size) {
        return ELF_EIO;
    }

    *size_out = stat.size;
    return ELF_SUCCESS;
}

static int validate_header(const struct elf32_header *header, uint32_t file_size) {
    uint32_t ph_table_size;

    if (file_size < sizeof(*header)) {
        return ELF_EINVAL;
    }
    if (header->ident[0] != ELF_MAGIC0 ||
        header->ident[1] != ELF_MAGIC1 ||
        header->ident[2] != ELF_MAGIC2 ||
        header->ident[3] != ELF_MAGIC3 ||
        header->ident[4] != ELF_CLASS_32 ||
        header->ident[5] != ELF_DATA_LSB ||
        header->ident[6] != ELF_VERSION_CURRENT) {
        return ELF_EINVAL;
    }
    if (header->type != ELF_TYPE_EXEC ||
        header->machine != ELF_MACHINE_386 ||
        header->version != ELF_VERSION_CURRENT ||
        header->ehsize != sizeof(struct elf32_header) ||
        header->phentsize != sizeof(struct elf32_program_header) ||
        header->phnum == 0 ||
        header->phnum > ELF_MAX_SEGMENTS) {
        return ELF_EINVAL;
    }

    ph_table_size = (uint32_t)header->phnum * sizeof(struct elf32_program_header);
    if (!range_inside_file(header->phoff, ph_table_size, file_size)) {
        return ELF_EINVAL;
    }

    return ELF_SUCCESS;
}

static int validate_segment(const struct elf32_program_header *ph, uint32_t file_size) {
    if (ph->type != ELF_PT_LOAD) {
        return ELF_SUCCESS;
    }
    if (ph->memsz == 0 || ph->filesz > ph->memsz) {
        return ELF_EINVAL;
    }
    if (ph->memsz > ELF_MAX_LOAD_MEMORY_SIZE) {
        return ELF_EINVAL;
    }
    if (!range_inside_file(ph->offset, ph->filesz, file_size)) {
        return ELF_EINVAL;
    }
    if (!range_inside_user_space(ph->vaddr, ph->memsz)) {
        return ELF_EINVAL;
    }
    if (!power_of_two_or_zero(ph->align)) {
        return ELF_EINVAL;
    }
    if (ph->align > 1 && ((ph->vaddr & (ph->align - 1)) != (ph->offset & (ph->align - 1)))) {
        return ELF_EINVAL;
    }

    return ELF_SUCCESS;
}

static int ranges_overlap(uint32_t a_start, uint32_t a_end, uint32_t b_start, uint32_t b_end) {
    return a_start < b_end && b_start < a_end;
}

static int map_segment_pages(uint32_t vaddr, uint32_t memsz) {
    uint32_t start = align_down(vaddr);
    uint32_t end = align_up(vaddr + memsz);
    uint32_t mapped_pages[ELF_MAX_LOAD_MEMORY_SIZE / FRAME_SIZE + 1];
    uint32_t mapped_phys[ELF_MAX_LOAD_MEMORY_SIZE / FRAME_SIZE + 1];
    uint32_t mapped_count = 0;

    for (uint32_t page = start; page < end; page += FRAME_SIZE) {
        if (paging_is_mapped(page)) {
            return ELF_EINVAL;
        }

        uint32_t phys = frame_alloc();
        if (!phys) {
            for (uint32_t i = 0; i < mapped_count; i++) {
                paging_unmap_page(mapped_pages[i]);
                frame_free(mapped_phys[i]);
            }
            return ELF_ENOSPC;
        }
        if (!paging_map_page_in_directory(paging_get_current_directory(), page,
                                          phys, PAGE_PRESENT | PAGE_WRITABLE |
                                                    PAGE_USER)) {
            frame_release(phys);
            for (uint32_t i = 0; i < mapped_count; i++) {
                paging_unmap_page(mapped_pages[i]);
                frame_release(mapped_phys[i]);
            }
            return ELF_ENOSPC;
        }
        if (!paging_is_user_writable(page)) {
            frame_free(phys);
            for (uint32_t i = 0; i < mapped_count; i++) {
                paging_unmap_page(mapped_pages[i]);
                frame_free(mapped_phys[i]);
            }
            return ELF_EIO;
        }

        mapped_pages[mapped_count] = page;
        mapped_phys[mapped_count] = phys;
        mapped_count++;
    }

    return ELF_SUCCESS;
}

static int protect_segment_pages(uint32_t vaddr, uint32_t memsz,
                                 uint32_t elf_flags) {
    uint32_t start = align_down(vaddr);
    uint32_t end = align_up(vaddr + memsz);
    uint32_t flags = PAGE_PRESENT | PAGE_USER;

    if (elf_flags & ELF_PF_W) {
        flags |= PAGE_WRITABLE;
    }
    for (uint32_t page = start; page < end; page += FRAME_SIZE) {
        if (!paging_set_page_flags_in_directory(
                paging_get_current_directory(), page, flags)) {
            return ELF_EIO;
        }
    }
    return ELF_SUCCESS;
}

int elf_load_from_vfs(const char *path, struct elf_load_info *out) {
    const struct elf32_header *header;
    uint32_t segment_page_starts[ELF_MAX_SEGMENTS];
    uint32_t segment_page_ends[ELF_MAX_SEGMENTS];
    uint32_t file_size;
    uint32_t segment_count = 0;
    uint32_t loaded_bytes = 0;
    uint32_t load_memory = 0;
    uint32_t load_start = 0xFFFFFFFFu;
    uint32_t load_end = 0;
    int status;

    if (!out) {
        return ELF_EINVAL;
    }

    memset(out, 0, sizeof(*out));

    status = read_file_into_buffer(path, &file_size);
    if (status != ELF_SUCCESS) {
        return status;
    }

    header = (const struct elf32_header *)elf_file_buffer;
    status = validate_header(header, file_size);
    if (status != ELF_SUCCESS) {
        return status;
    }

    for (uint32_t i = 0; i < header->phnum; i++) {
        const struct elf32_program_header *ph =
            (const struct elf32_program_header *)(elf_file_buffer + header->phoff +
                                                  i * sizeof(struct elf32_program_header));
        status = validate_segment(ph, file_size);
        if (status != ELF_SUCCESS) {
            return status;
        }
        if (ph->type == ELF_PT_LOAD) {
            uint32_t page_start = align_down(ph->vaddr);
            uint32_t page_end = align_up(ph->vaddr + ph->memsz);

            for (uint32_t j = 0; j < segment_count; j++) {
                if (ranges_overlap(page_start, page_end,
                                   segment_page_starts[j], segment_page_ends[j])) {
                    return ELF_EINVAL;
                }
            }

            segment_page_starts[segment_count] = page_start;
            segment_page_ends[segment_count] = page_end;
            segment_count++;
            if (ph->memsz > ELF_MAX_LOAD_MEMORY_SIZE - load_memory) {
                return ELF_EINVAL;
            }
            load_memory += ph->memsz;
            if (ph->vaddr < load_start) {
                load_start = ph->vaddr;
            }
            if (ph->vaddr + ph->memsz > load_end) {
                load_end = ph->vaddr + ph->memsz;
            }
        }
    }

    if (segment_count == 0 ||
        header->entry < load_start ||
        header->entry >= load_end) {
        return ELF_EINVAL;
    }

    for (uint32_t i = 0; i < header->phnum; i++) {
        const struct elf32_program_header *ph =
            (const struct elf32_program_header *)(elf_file_buffer + header->phoff +
                                                  i * sizeof(struct elf32_program_header));
        if (ph->type != ELF_PT_LOAD) {
            continue;
        }

        status = map_segment_pages(ph->vaddr, ph->memsz);
        if (status != ELF_SUCCESS) {
            return status;
        }

        memset((void *)ph->vaddr, 0, ph->memsz);
        memcpy((void *)ph->vaddr, elf_file_buffer + ph->offset, ph->filesz);
        status = protect_segment_pages(ph->vaddr, ph->memsz, ph->flags);
        if (status != ELF_SUCCESS) {
            return status;
        }
        loaded_bytes += ph->filesz;
    }

    out->entry = header->entry;
    out->load_start = load_start;
    out->load_end = load_end;
    out->loaded_bytes = loaded_bytes;
    out->segment_count = segment_count;
    return ELF_SUCCESS;
}

int elf_load_from_vfs_into(uint32_t cr3, const char *path, struct elf_load_info *out) {
    uint32_t old_cr3;
    int status;

    if (!cr3) {
        return ELF_EINVAL;
    }

    old_cr3 = paging_get_current_directory();
    paging_switch_directory(cr3);
    status = elf_load_from_vfs(path, out);
    paging_switch_directory(old_cr3);
    return status;
}
