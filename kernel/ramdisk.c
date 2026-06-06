#include "ramdisk.h"
#include "blkdev.h"
#include "frame.h"
#include "paging.h"
#include "string.h"
#include "serial.h"
#include <stdint.h>

#define RAMDISK_BASE 0x00C00000u
#define RAMDISK_SIZE (2 * 1024 * 1024u)
#define RAMDISK_END (RAMDISK_BASE + RAMDISK_SIZE)
#define RAMDISK_SECTORS (RAMDISK_SIZE / SECTOR_SIZE)

static uint8_t *ramdisk_data = (uint8_t *)RAMDISK_BASE;
static struct block_device ramdisk_dev;
static int ramdisk_initialized = 0;

static int ramdisk_range_valid(uint32_t lba, uint32_t count) {
    if (count == 0 || lba >= RAMDISK_SECTORS) {
        return 0;
    }
    return count <= (RAMDISK_SECTORS - lba);
}

static int ramdisk_map_pages(void) {
    for (uint32_t addr = RAMDISK_BASE; addr < RAMDISK_END; addr += FRAME_SIZE) {
        paging_map_page(addr, addr, PAGE_PRESENT | PAGE_WRITABLE);
        if (!paging_is_mapped(addr)) {
            return BLKDEV_ERROR;
        }
    }
    return BLKDEV_SUCCESS;
}

static int ramdisk_read_sector(struct block_device *dev, uint32_t lba, void *buffer) {
    (void)dev;
    if (!buffer) {
        return BLKDEV_EINVAL;
    }

    if (!ramdisk_range_valid(lba, 1)) {
        return BLKDEV_EINVAL;
    }

    uint8_t *src = ramdisk_data + (lba * SECTOR_SIZE);
    memcpy(buffer, src, SECTOR_SIZE);
    return BLKDEV_SUCCESS;
}

static int ramdisk_write_sector(struct block_device *dev, uint32_t lba, const void *buffer) {
    (void)dev;
    if (!buffer) {
        return BLKDEV_EINVAL;
    }

    if (!ramdisk_range_valid(lba, 1)) {
        return BLKDEV_EINVAL;
    }

    uint8_t *dst = ramdisk_data + (lba * SECTOR_SIZE);
    memcpy(dst, buffer, SECTOR_SIZE);
    return BLKDEV_SUCCESS;
}

static int ramdisk_read_sectors(struct block_device *dev, uint32_t lba, uint32_t count, void *buffer) {
    (void)dev;
    if (!buffer || count == 0) {
        return BLKDEV_EINVAL;
    }

    if (!ramdisk_range_valid(lba, count)) {
        return BLKDEV_EINVAL;
    }

    uint8_t *src = ramdisk_data + (lba * SECTOR_SIZE);
    memcpy(buffer, src, count * SECTOR_SIZE);
    return BLKDEV_SUCCESS;
}

static int ramdisk_write_sectors(struct block_device *dev, uint32_t lba, uint32_t count, const void *buffer) {
    (void)dev;
    if (!buffer || count == 0) {
        return BLKDEV_EINVAL;
    }

    if (!ramdisk_range_valid(lba, count)) {
        return BLKDEV_EINVAL;
    }

    uint8_t *dst = ramdisk_data + (lba * SECTOR_SIZE);
    memcpy(dst, buffer, count * SECTOR_SIZE);
    return BLKDEV_SUCCESS;
}

int ramdisk_init(void) {
    if (ramdisk_initialized) {
        return BLKDEV_SUCCESS;
    }

    if (!frame_reserve_range(RAMDISK_BASE, RAMDISK_END)) {
        serial_puts("RAMDISK_INIT_FAIL\n");
        return BLKDEV_ERROR;
    }

    if (ramdisk_map_pages() != BLKDEV_SUCCESS) {
        serial_puts("RAMDISK_INIT_FAIL\n");
        return BLKDEV_ERROR;
    }

    memset(ramdisk_data, 0, RAMDISK_SIZE);

    memset(&ramdisk_dev, 0, sizeof(ramdisk_dev));

    const char *name = "ramdisk0";
    for (int i = 0; i < BLKDEV_MAX_NAME - 1 && name[i]; i++) {
        ramdisk_dev.name[i] = name[i];
    }

    ramdisk_dev.type = BLKDEV_TYPE_RAMDISK;
    ramdisk_dev.sector_count = RAMDISK_SECTORS;
    ramdisk_dev.sector_size = SECTOR_SIZE;
    ramdisk_dev.read_sector = ramdisk_read_sector;
    ramdisk_dev.write_sector = ramdisk_write_sector;
    ramdisk_dev.read_sectors = ramdisk_read_sectors;
    ramdisk_dev.write_sectors = ramdisk_write_sectors;
    ramdisk_dev.private_data = NULL;

    ramdisk_initialized = 1;
    serial_puts("RAMDISK_INIT_OK\n");
    serial_puts("Ramdisk initialized: ");
    serial_put_uint32(RAMDISK_SECTORS);
    serial_puts(" sectors (");
    serial_put_uint32(RAMDISK_SIZE / 1024);
    serial_puts(" KB)\n");

    return BLKDEV_SUCCESS;
}

struct block_device *ramdisk_get_device(void) {
    return ramdisk_initialized ? &ramdisk_dev : 0;
}
