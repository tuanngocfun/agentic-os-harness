#include "simplefs.h"
#include "vfs.h"
#include "string.h"
#include <stddef.h>
#include <stdint.h>

struct simplefs_superblock {
    uint32_t magic;
    uint32_t version;
    uint32_t sector_size;
    uint32_t total_sectors;
    uint32_t dir_start;
    uint32_t dir_sectors;
    uint32_t data_start;
    uint32_t next_free_sector;
    uint32_t max_files;
};

struct simplefs_dir_entry {
    uint32_t used;
    char name[SIMPLEFS_NAME_MAX];
    uint32_t start_sector;
    uint32_t size_bytes;
    uint32_t allocated_sectors;
    uint32_t flags;
    uint32_t reserved[3];
};

static struct block_device *simplefs_dev;
static struct simplefs_superblock simplefs_sb;
static int simplefs_mounted;

static uint32_t simplefs_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t simplefs_sectors_for_bytes(uint32_t bytes) {
    if (bytes == 0) {
        return 0;
    }
    return (bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
}

static int simplefs_validate_dev(struct block_device *dev) {
    if (!dev || !dev->read_sector || !dev->write_sector) {
        return VFS_EINVAL;
    }
    if (dev->sector_size != SECTOR_SIZE || dev->sector_count <= SIMPLEFS_DATA_START) {
        return VFS_EINVAL;
    }
    return VFS_SUCCESS;
}

static int simplefs_normalize_path(const char *path, char *name_out) {
    const char *name = path;
    uint32_t len = 0;

    if (!path || !name_out) {
        return VFS_EINVAL;
    }

    if (path[0] == '/') {
        name = path + 1;
    }

    if (name[0] == '\0') {
        return VFS_EINVAL;
    }

    while (name[len]) {
        if (name[len] == '/') {
            return VFS_EINVAL;
        }
        if (len >= SIMPLEFS_NAME_MAX - 1) {
            return VFS_EINVAL;
        }
        name_out[len] = name[len];
        len++;
    }

    name_out[len] = '\0';
    return VFS_SUCCESS;
}

static int simplefs_write_superblock(void) {
    uint8_t sector[SECTOR_SIZE];

    memset(sector, 0, sizeof(sector));
    memcpy(sector, &simplefs_sb, sizeof(simplefs_sb));

    if (simplefs_dev->write_sector(simplefs_dev, 0, sector) != BLKDEV_SUCCESS) {
        return VFS_EIO;
    }
    return VFS_SUCCESS;
}

static int simplefs_read_entry(uint32_t index, struct simplefs_dir_entry *entry) {
    uint8_t sector[SECTOR_SIZE];
    uint32_t entries_per_sector = SECTOR_SIZE / sizeof(struct simplefs_dir_entry);
    uint32_t sector_lba = simplefs_sb.dir_start + (index / entries_per_sector);
    uint32_t sector_offset = (index % entries_per_sector) * sizeof(struct simplefs_dir_entry);

    if (!entry || index >= SIMPLEFS_MAX_FILES) {
        return VFS_EINVAL;
    }

    if (simplefs_dev->read_sector(simplefs_dev, sector_lba, sector) != BLKDEV_SUCCESS) {
        return VFS_EIO;
    }

    memcpy(entry, sector + sector_offset, sizeof(*entry));
    return VFS_SUCCESS;
}

static int simplefs_write_entry(uint32_t index, const struct simplefs_dir_entry *entry) {
    uint8_t sector[SECTOR_SIZE];
    uint32_t entries_per_sector = SECTOR_SIZE / sizeof(struct simplefs_dir_entry);
    uint32_t sector_lba = simplefs_sb.dir_start + (index / entries_per_sector);
    uint32_t sector_offset = (index % entries_per_sector) * sizeof(struct simplefs_dir_entry);

    if (!entry || index >= SIMPLEFS_MAX_FILES) {
        return VFS_EINVAL;
    }

    if (simplefs_dev->read_sector(simplefs_dev, sector_lba, sector) != BLKDEV_SUCCESS) {
        return VFS_EIO;
    }

    memcpy(sector + sector_offset, entry, sizeof(*entry));

    if (simplefs_dev->write_sector(simplefs_dev, sector_lba, sector) != BLKDEV_SUCCESS) {
        return VFS_EIO;
    }

    return VFS_SUCCESS;
}

static int simplefs_find_entry(const char *name, uint32_t *index_out, struct simplefs_dir_entry *entry_out) {
    struct simplefs_dir_entry entry;

    for (uint32_t i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        int status = simplefs_read_entry(i, &entry);
        if (status != VFS_SUCCESS) {
            return status;
        }

        if (entry.used && strcmp(entry.name, name) == 0) {
            if (index_out) {
                *index_out = i;
            }
            if (entry_out) {
                *entry_out = entry;
            }
            return VFS_SUCCESS;
        }
    }

    return VFS_ENOENT;
}

static int simplefs_find_free_entry(uint32_t *index_out) {
    struct simplefs_dir_entry entry;

    if (!index_out) {
        return VFS_EINVAL;
    }

    for (uint32_t i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        int status = simplefs_read_entry(i, &entry);
        if (status != VFS_SUCCESS) {
            return status;
        }
        if (!entry.used) {
            *index_out = i;
            return VFS_SUCCESS;
        }
    }

    return VFS_ENOSPC;
}

static int simplefs_allocate_sectors(uint32_t count, uint32_t *start_out) {
    if (!start_out) {
        return VFS_EINVAL;
    }

    if (count == 0) {
        *start_out = 0;
        return VFS_SUCCESS;
    }

    if (simplefs_sb.next_free_sector >= simplefs_sb.total_sectors ||
        count > (simplefs_sb.total_sectors - simplefs_sb.next_free_sector)) {
        return VFS_ENOSPC;
    }

    *start_out = simplefs_sb.next_free_sector;
    simplefs_sb.next_free_sector += count;
    return simplefs_write_superblock();
}

static int simplefs_zero_run(uint32_t start_sector, uint32_t count) {
    uint8_t sector[SECTOR_SIZE];

    memset(sector, 0, sizeof(sector));

    for (uint32_t i = 0; i < count; i++) {
        if (simplefs_dev->write_sector(simplefs_dev, start_sector + i, sector) != BLKDEV_SUCCESS) {
            return VFS_EIO;
        }
    }

    return VFS_SUCCESS;
}

static int simplefs_copy_file_data(const struct simplefs_dir_entry *entry, uint32_t new_start) {
    uint8_t sector[SECTOR_SIZE];
    uint32_t sectors = simplefs_sectors_for_bytes(entry->size_bytes);

    for (uint32_t i = 0; i < sectors; i++) {
        if (simplefs_dev->read_sector(simplefs_dev, entry->start_sector + i, sector) != BLKDEV_SUCCESS) {
            return VFS_EIO;
        }
        if (simplefs_dev->write_sector(simplefs_dev, new_start + i, sector) != BLKDEV_SUCCESS) {
            return VFS_EIO;
        }
    }

    return VFS_SUCCESS;
}

static int simplefs_ensure_capacity(struct simplefs_dir_entry *entry, uint32_t required_sectors) {
    uint32_t new_start;
    int status;

    if (required_sectors <= entry->allocated_sectors) {
        return VFS_SUCCESS;
    }

    status = simplefs_allocate_sectors(required_sectors, &new_start);
    if (status != VFS_SUCCESS) {
        return status;
    }

    status = simplefs_zero_run(new_start, required_sectors);
    if (status != VFS_SUCCESS) {
        return status;
    }

    if (entry->size_bytes > 0 && entry->allocated_sectors > 0) {
        status = simplefs_copy_file_data(entry, new_start);
        if (status != VFS_SUCCESS) {
            return status;
        }
    }

    entry->start_sector = new_start;
    entry->allocated_sectors = required_sectors;
    return VFS_SUCCESS;
}

static void simplefs_copy_name(char *dest, const char *src) {
    uint32_t i = 0;

    while (i < SIMPLEFS_NAME_MAX - 1 && src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

int simplefs_format(struct block_device *dev) {
    uint8_t sector[SECTOR_SIZE];

    if (simplefs_validate_dev(dev) != VFS_SUCCESS) {
        return VFS_EINVAL;
    }

    simplefs_dev = dev;
    memset(&simplefs_sb, 0, sizeof(simplefs_sb));
    simplefs_sb.magic = SIMPLEFS_MAGIC;
    simplefs_sb.version = SIMPLEFS_VERSION;
    simplefs_sb.sector_size = SECTOR_SIZE;
    simplefs_sb.total_sectors = dev->sector_count;
    simplefs_sb.dir_start = SIMPLEFS_DIR_START;
    simplefs_sb.dir_sectors = SIMPLEFS_DIR_SECTORS;
    simplefs_sb.data_start = SIMPLEFS_DATA_START;
    simplefs_sb.next_free_sector = SIMPLEFS_DATA_START;
    simplefs_sb.max_files = SIMPLEFS_MAX_FILES;

    memset(sector, 0, sizeof(sector));
    memcpy(sector, &simplefs_sb, sizeof(simplefs_sb));
    if (dev->write_sector(dev, 0, sector) != BLKDEV_SUCCESS) {
        return VFS_EIO;
    }

    memset(sector, 0, sizeof(sector));
    for (uint32_t i = 0; i < SIMPLEFS_DIR_SECTORS; i++) {
        if (dev->write_sector(dev, SIMPLEFS_DIR_START + i, sector) != BLKDEV_SUCCESS) {
            return VFS_EIO;
        }
    }

    simplefs_mounted = 1;
    return VFS_SUCCESS;
}

int simplefs_mount(struct block_device *dev) {
    uint8_t sector[SECTOR_SIZE];

    if (simplefs_validate_dev(dev) != VFS_SUCCESS) {
        return VFS_EINVAL;
    }

    if (dev->read_sector(dev, 0, sector) != BLKDEV_SUCCESS) {
        return VFS_EIO;
    }

    memcpy(&simplefs_sb, sector, sizeof(simplefs_sb));
    if (simplefs_sb.magic != SIMPLEFS_MAGIC ||
        simplefs_sb.version != SIMPLEFS_VERSION ||
        simplefs_sb.sector_size != SECTOR_SIZE ||
        simplefs_sb.total_sectors != dev->sector_count ||
        simplefs_sb.dir_start != SIMPLEFS_DIR_START ||
        simplefs_sb.dir_sectors != SIMPLEFS_DIR_SECTORS ||
        simplefs_sb.data_start != SIMPLEFS_DATA_START ||
        simplefs_sb.max_files != SIMPLEFS_MAX_FILES ||
        simplefs_sb.next_free_sector < SIMPLEFS_DATA_START ||
        simplefs_sb.next_free_sector > dev->sector_count) {
        return VFS_EIO;
    }

    simplefs_dev = dev;
    simplefs_mounted = 1;
    return VFS_SUCCESS;
}

int simplefs_open(const char *path, uint32_t flags, struct simplefs_file_info *out) {
    char name[SIMPLEFS_NAME_MAX];
    struct simplefs_dir_entry entry;
    uint32_t index;
    int status;

    if (!simplefs_mounted) {
        return VFS_EIO;
    }

    status = simplefs_normalize_path(path, name);
    if (status != VFS_SUCCESS) {
        return status;
    }

    status = simplefs_find_entry(name, &index, &entry);
    if (status == VFS_ENOENT) {
        if ((flags & VFS_O_CREAT) == 0) {
            return VFS_ENOENT;
        }

        status = simplefs_find_free_entry(&index);
        if (status != VFS_SUCCESS) {
            return status;
        }

        memset(&entry, 0, sizeof(entry));
        entry.used = 1;
        simplefs_copy_name(entry.name, name);

        status = simplefs_write_entry(index, &entry);
        if (status != VFS_SUCCESS) {
            return status;
        }
    } else if (status != VFS_SUCCESS) {
        return status;
    } else if (flags & VFS_O_TRUNC) {
        entry.start_sector = 0;
        entry.size_bytes = 0;
        entry.allocated_sectors = 0;
        status = simplefs_write_entry(index, &entry);
        if (status != VFS_SUCCESS) {
            return status;
        }
    }

    if (out) {
        out->start_sector = entry.start_sector;
        out->size = entry.size_bytes;
        out->allocated_sectors = entry.allocated_sectors;
        out->flags = entry.flags;
    }

    return VFS_SUCCESS;
}

int simplefs_read(const char *path, uint32_t offset, void *buffer, uint32_t count) {
    char name[SIMPLEFS_NAME_MAX];
    struct simplefs_dir_entry entry;
    uint8_t sector[SECTOR_SIZE];
    uint8_t *out = (uint8_t *)buffer;
    uint32_t done = 0;
    int status;

    if (!simplefs_mounted) {
        return VFS_EIO;
    }

    if (!buffer && count > 0) {
        return VFS_EINVAL;
    }

    status = simplefs_normalize_path(path, name);
    if (status != VFS_SUCCESS) {
        return status;
    }

    status = simplefs_find_entry(name, 0, &entry);
    if (status != VFS_SUCCESS) {
        return status;
    }

    if (offset >= entry.size_bytes || count == 0) {
        return 0;
    }

    count = simplefs_min_u32(count, entry.size_bytes - offset);
    while (done < count) {
        uint32_t absolute = offset + done;
        uint32_t sector_index = absolute / SECTOR_SIZE;
        uint32_t sector_offset = absolute % SECTOR_SIZE;
        uint32_t chunk = simplefs_min_u32(count - done, SECTOR_SIZE - sector_offset);

        if (simplefs_dev->read_sector(simplefs_dev, entry.start_sector + sector_index, sector) != BLKDEV_SUCCESS) {
            return VFS_EIO;
        }

        memcpy(out + done, sector + sector_offset, chunk);
        done += chunk;
    }

    return (int)done;
}

int simplefs_write(const char *path, uint32_t offset, const void *buffer, uint32_t count) {
    char name[SIMPLEFS_NAME_MAX];
    struct simplefs_dir_entry entry;
    uint8_t sector[SECTOR_SIZE];
    const uint8_t *in = (const uint8_t *)buffer;
    uint32_t index;
    uint32_t done = 0;
    uint32_t new_size;
    uint32_t required_sectors;
    int status;

    if (!simplefs_mounted) {
        return VFS_EIO;
    }

    if (!buffer && count > 0) {
        return VFS_EINVAL;
    }

    if (count == 0) {
        return 0;
    }

    if (offset + count < offset) {
        return VFS_EINVAL;
    }

    status = simplefs_normalize_path(path, name);
    if (status != VFS_SUCCESS) {
        return status;
    }

    status = simplefs_find_entry(name, &index, &entry);
    if (status != VFS_SUCCESS) {
        return status;
    }

    new_size = offset + count;
    if (new_size < entry.size_bytes) {
        new_size = entry.size_bytes;
    }

    required_sectors = simplefs_sectors_for_bytes(new_size);
    status = simplefs_ensure_capacity(&entry, required_sectors);
    if (status != VFS_SUCCESS) {
        return status;
    }

    while (done < count) {
        uint32_t absolute = offset + done;
        uint32_t sector_index = absolute / SECTOR_SIZE;
        uint32_t sector_offset = absolute % SECTOR_SIZE;
        uint32_t chunk = simplefs_min_u32(count - done, SECTOR_SIZE - sector_offset);

        if (chunk != SECTOR_SIZE) {
            if (simplefs_dev->read_sector(simplefs_dev, entry.start_sector + sector_index, sector) != BLKDEV_SUCCESS) {
                return VFS_EIO;
            }
        }

        memcpy(sector + sector_offset, in + done, chunk);

        if (simplefs_dev->write_sector(simplefs_dev, entry.start_sector + sector_index, sector) != BLKDEV_SUCCESS) {
            return VFS_EIO;
        }

        done += chunk;
    }

    entry.size_bytes = new_size;
    status = simplefs_write_entry(index, &entry);
    if (status != VFS_SUCCESS) {
        return status;
    }

    return (int)done;
}

int simplefs_stat(const char *path, struct simplefs_file_info *out) {
    char name[SIMPLEFS_NAME_MAX];
    struct simplefs_dir_entry entry;
    int status;

    if (!simplefs_mounted) {
        return VFS_EIO;
    }

    if (!out) {
        return VFS_EINVAL;
    }

    status = simplefs_normalize_path(path, name);
    if (status != VFS_SUCCESS) {
        return status;
    }

    status = simplefs_find_entry(name, 0, &entry);
    if (status != VFS_SUCCESS) {
        return status;
    }

    out->start_sector = entry.start_sector;
    out->size = entry.size_bytes;
    out->allocated_sectors = entry.allocated_sectors;
    out->flags = entry.flags;
    return VFS_SUCCESS;
}
