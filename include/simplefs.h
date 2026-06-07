#ifndef SIMPLEFS_H
#define SIMPLEFS_H

#include "blkdev.h"
#include <stdint.h>

#define SIMPLEFS_MAGIC 0x31534653u
#define SIMPLEFS_VERSION 1
#define SIMPLEFS_DIR_START 1
#define SIMPLEFS_DIR_SECTORS 4
#define SIMPLEFS_MAX_FILES 32
#define SIMPLEFS_NAME_MAX 32
#define SIMPLEFS_DATA_START (SIMPLEFS_DIR_START + SIMPLEFS_DIR_SECTORS)

struct simplefs_file_info {
    uint32_t start_sector;
    uint32_t size;
    uint32_t allocated_sectors;
    uint32_t flags;
};

int simplefs_format(struct block_device *dev);
int simplefs_mount(struct block_device *dev);
int simplefs_open(const char *path, uint32_t flags, struct simplefs_file_info *out);
int simplefs_read(const char *path, uint32_t offset, void *buffer, uint32_t count);
int simplefs_write(const char *path, uint32_t offset, const void *buffer, uint32_t count);
int simplefs_stat(const char *path, struct simplefs_file_info *out);

#endif
