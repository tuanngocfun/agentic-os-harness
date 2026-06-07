#include "vfs.h"
#include "simplefs.h"
#include "string.h"
#include <stdint.h>

struct vfs_open_file {
    int used;
    char path[VFS_MAX_PATH];
    uint32_t flags;
    uint32_t offset;
};

static struct block_device *vfs_root_dev;
static int vfs_mounted;
static struct vfs_open_file vfs_open_files[VFS_MAX_OPEN_FILES];

static int vfs_flags_valid(uint32_t flags) {
    uint32_t access = flags & VFS_O_RDWR;
    uint32_t allowed = VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC;

    if ((flags & ~allowed) != 0) {
        return 0;
    }

    if (access == 0) {
        return 0;
    }

    if ((flags & VFS_O_TRUNC) && ((flags & VFS_O_WRONLY) == 0)) {
        return 0;
    }

    if ((flags & VFS_O_CREAT) && ((flags & VFS_O_WRONLY) == 0)) {
        return 0;
    }

    return 1;
}

static int vfs_copy_path(const char *path, char *out) {
    uint32_t i = 0;

    if (!path || !out) {
        return VFS_EINVAL;
    }

    while (path[i]) {
        if (i >= VFS_MAX_PATH - 1) {
            return VFS_EINVAL;
        }
        out[i] = path[i];
        i++;
    }

    out[i] = '\0';
    return VFS_SUCCESS;
}

static int vfs_alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!vfs_open_files[i].used) {
            return i;
        }
    }
    return VFS_EMFILE;
}

static int vfs_fd_valid(int fd) {
    return fd >= 0 && fd < VFS_MAX_OPEN_FILES && vfs_open_files[fd].used;
}

static void vfs_clear_open_files(void) {
    memset(vfs_open_files, 0, sizeof(vfs_open_files));
}

int vfs_init(struct block_device *dev) {
    if (!dev || dev->sector_size != SECTOR_SIZE) {
        return VFS_EINVAL;
    }

    vfs_root_dev = dev;
    vfs_mounted = 0;
    vfs_clear_open_files();
    return VFS_SUCCESS;
}

int vfs_format(void) {
    if (!vfs_root_dev) {
        return VFS_EINVAL;
    }

    vfs_clear_open_files();
    vfs_mounted = 0;
    return simplefs_format(vfs_root_dev);
}

int vfs_mount(void) {
    int status;

    if (!vfs_root_dev) {
        return VFS_EINVAL;
    }

    status = simplefs_mount(vfs_root_dev);
    if (status != VFS_SUCCESS) {
        vfs_mounted = 0;
        return status;
    }

    vfs_mounted = 1;
    return VFS_SUCCESS;
}

int vfs_open(const char *path, uint32_t flags) {
    struct simplefs_file_info info;
    int fd;
    int status;
    char path_copy[VFS_MAX_PATH];

    if (!vfs_mounted) {
        return VFS_EIO;
    }

    if (!vfs_flags_valid(flags)) {
        return VFS_EINVAL;
    }

    status = vfs_copy_path(path, path_copy);
    if (status != VFS_SUCCESS) {
        return status;
    }

    fd = vfs_alloc_fd();
    if (fd < 0) {
        return fd;
    }

    status = simplefs_open(path_copy, flags, &info);
    if (status != VFS_SUCCESS) {
        return status;
    }

    vfs_open_files[fd].used = 1;
    vfs_open_files[fd].flags = flags;
    vfs_open_files[fd].offset = 0;
    memcpy(vfs_open_files[fd].path, path_copy, VFS_MAX_PATH);
    return fd;
}

int vfs_read(int fd, void *buffer, uint32_t count) {
    int status;

    if (!vfs_fd_valid(fd)) {
        return VFS_EBADF;
    }

    if (!buffer && count > 0) {
        return VFS_EINVAL;
    }

    if ((vfs_open_files[fd].flags & VFS_O_RDONLY) == 0) {
        return VFS_EBADF;
    }

    status = simplefs_read(vfs_open_files[fd].path, vfs_open_files[fd].offset, buffer, count);
    if (status > 0) {
        vfs_open_files[fd].offset += (uint32_t)status;
    }

    return status;
}

int vfs_write(int fd, const void *buffer, uint32_t count) {
    int status;

    if (!vfs_fd_valid(fd)) {
        return VFS_EBADF;
    }

    if (!buffer && count > 0) {
        return VFS_EINVAL;
    }

    if ((vfs_open_files[fd].flags & VFS_O_WRONLY) == 0) {
        return VFS_EBADF;
    }

    status = simplefs_write(vfs_open_files[fd].path, vfs_open_files[fd].offset, buffer, count);
    if (status > 0) {
        vfs_open_files[fd].offset += (uint32_t)status;
    }

    return status;
}

int vfs_close(int fd) {
    if (!vfs_fd_valid(fd)) {
        return VFS_EBADF;
    }

    memset(&vfs_open_files[fd], 0, sizeof(vfs_open_files[fd]));
    return VFS_SUCCESS;
}

int vfs_stat(const char *path, struct vfs_stat *out) {
    struct simplefs_file_info info;
    int status;

    if (!vfs_mounted) {
        return VFS_EIO;
    }

    if (!out) {
        return VFS_EINVAL;
    }

    status = simplefs_stat(path, &info);
    if (status != VFS_SUCCESS) {
        return status;
    }

    out->size = info.size;
    out->start_sector = info.start_sector;
    out->allocated_sectors = info.allocated_sectors;
    out->flags = info.flags;
    return VFS_SUCCESS;
}
