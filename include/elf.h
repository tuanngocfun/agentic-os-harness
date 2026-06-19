#ifndef ELF_H
#define ELF_H

#include <stdint.h>

#define ELF_SUCCESS 0
#define ELF_EINVAL  -1
#define ELF_ENOENT  -2
#define ELF_EIO     -3
#define ELF_ENOSPC  -4

#define ELF_MAX_SEGMENTS 8
#define ELF_MAX_IMAGE_SIZE 8192
#define ELF_MAX_LOAD_MEMORY_SIZE 65536
#define ELF_USER_LOAD_MIN 0x40000000u
#define ELF_USER_LOAD_MAX 0xB0000000u

struct elf_load_info {
    uint32_t entry;
    uint32_t load_start;
    uint32_t load_end;
    uint32_t loaded_bytes;
    uint32_t segment_count;
};

int elf_load_from_vfs(const char *path, struct elf_load_info *out);
int elf_load_from_vfs_into(uint32_t cr3, const char *path, struct elf_load_info *out);

#endif
