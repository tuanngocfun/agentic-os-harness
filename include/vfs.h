#ifndef VFS_H
#define VFS_H

#include "blkdev.h"
#include <stdint.h>

#define VFS_MAX_OPEN_FILES 16
#define VFS_MAX_PATH 64
#define VFS_MAX_NAME 32

#define VFS_O_RDONLY 0x0001
#define VFS_O_WRONLY 0x0002
#define VFS_O_RDWR   (VFS_O_RDONLY | VFS_O_WRONLY)
#define VFS_O_CREAT  0x0100
#define VFS_O_TRUNC  0x0200

#define VFS_SUCCESS 0
#define VFS_EINVAL  -1
#define VFS_ENOENT  -2
#define VFS_ENOSPC  -3
#define VFS_EIO     -4
#define VFS_EMFILE  -5
#define VFS_EBADF   -6
#define VFS_EEXIST  -7

struct vfs_stat {
    uint32_t size;
    uint32_t start_sector;
    uint32_t allocated_sectors;
    uint32_t flags;
};

int vfs_init(struct block_device *dev);
int vfs_format(void);
int vfs_mount(void);
int vfs_open(const char *path, uint32_t flags);
int vfs_read(int fd, void *buffer, uint32_t count);
int vfs_write(int fd, const void *buffer, uint32_t count);
int vfs_close(int fd);
int vfs_stat(const char *path, struct vfs_stat *out);

#endif
