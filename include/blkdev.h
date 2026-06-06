#ifndef BLKDEV_H
#define BLKDEV_H

#include <stdint.h>

#define SECTOR_SIZE 512
#define BLKDEV_MAX_NAME 32

typedef enum {
    BLKDEV_TYPE_RAMDISK,
    BLKDEV_TYPE_ATA,
    BLKDEV_TYPE_VIRTIO
} blkdev_type_t;

struct block_device {
    char name[BLKDEV_MAX_NAME];
    blkdev_type_t type;
    uint32_t sector_count;
    uint32_t sector_size;

    // Operations
    int (*read_sector)(struct block_device *dev, uint32_t lba, void *buffer);
    int (*write_sector)(struct block_device *dev, uint32_t lba, const void *buffer);
    int (*read_sectors)(struct block_device *dev, uint32_t lba, uint32_t count, void *buffer);
    int (*write_sectors)(struct block_device *dev, uint32_t lba, uint32_t count, const void *buffer);

    // Private data
    void *private_data;
};

// Block device errors
#define BLKDEV_SUCCESS 0
#define BLKDEV_ERROR -1
#define BLKDEV_EINVAL -2  // Invalid parameter
#define BLKDEV_EIO -3     // I/O error

#endif
