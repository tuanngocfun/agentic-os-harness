# Storage Foundation Design - Ramdisk Block Device

**Date:** 2026-06-06  
**Phase:** P0 - Storage Foundation  
**Step:** 1/3 - Ramdisk Block Device

---

## Architecture Overview

### Ramdisk Design

A ramdisk is the simplest block device - it's just a region of memory treated as a disk. No hardware driver complexity, perfect for testing filesystem code.

```
┌─────────────────────────────────────┐
│     Block Device Interface          │  ← Generic API
├─────────────────────────────────────┤
│       Ramdisk Driver                │  ← Implementation
├─────────────────────────────────────┤
│  Reserved mapped range @ 0x00C00000 │  ← Storage
└─────────────────────────────────────┘
```

### Key Design Decisions

1. **Location:** Place ramdisk at 0x00C00000 (12MB), away from low page-table frames and existing usermode test pages
2. **Size:** 2MB (4096 sectors × 512 bytes) - enough for simple filesystem
3. **Sector Size:** 512 bytes (standard block size)
4. **Interface:** Standard block device API (`read_sector`, `write_sector`)
5. **Safety:** Reserve frames with `frame_reserve_range()` and identity-map pages before clearing the ramdisk

### Memory Map (Updated)

```
0x00000000 - 0x000FFFFF: Low memory (1MB)
0x00100000 - 0x001FFFFF: Kernel code/data
0x00200000 - 0x002FFFFF: Kernel heap (1MB)
0x00300000 - 0x003FFFFF: Paging structures
0x00400000 - 0x00BFFFFF: Available/test mappings
0x00C00000 - 0x00DFFFFF: Ramdisk (2MB)    ← NEW
0x00E00000+           : Available memory
```

---

## Interface Design

### Block Device Structure

```c
// include/blkdev.h

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
```

### Ramdisk Implementation

```c
// kernel/ramdisk.c

#define RAMDISK_BASE 0x00C00000
#define RAMDISK_SIZE (2 * 1024 * 1024)  // 2MB
#define RAMDISK_END (RAMDISK_BASE + RAMDISK_SIZE)
#define RAMDISK_SECTORS (RAMDISK_SIZE / SECTOR_SIZE)

static uint8_t *ramdisk_data = (uint8_t *)RAMDISK_BASE;

static int ramdisk_read_sector(struct block_device *dev, uint32_t lba, void *buffer) {
    if (lba >= RAMDISK_SECTORS) {
        return -1;  // Out of bounds
    }
    
    uint8_t *src = ramdisk_data + (lba * SECTOR_SIZE);
    memcpy(buffer, src, SECTOR_SIZE);
    return 0;
}

static int ramdisk_write_sector(struct block_device *dev, uint32_t lba, const void *buffer) {
    if (lba >= RAMDISK_SECTORS) {
        return -1;  // Out of bounds
    }
    
    uint8_t *dst = ramdisk_data + (lba * SECTOR_SIZE);
    memcpy(dst, buffer, SECTOR_SIZE);
    return 0;
}

static int ramdisk_read_sectors(struct block_device *dev, uint32_t lba, uint32_t count, void *buffer) {
    if (count == 0 || lba >= RAMDISK_SECTORS || count > (RAMDISK_SECTORS - lba)) {
        return -1;  // Out of bounds
    }
    
    uint8_t *src = ramdisk_data + (lba * SECTOR_SIZE);
    memcpy(buffer, src, count * SECTOR_SIZE);
    return 0;
}

static int ramdisk_write_sectors(struct block_device *dev, uint32_t lba, uint32_t count, const void *buffer) {
    if (count == 0 || lba >= RAMDISK_SECTORS || count > (RAMDISK_SECTORS - lba)) {
        return -1;  // Out of bounds
    }
    
    uint8_t *dst = ramdisk_data + (lba * SECTOR_SIZE);
    memcpy(dst, buffer, count * SECTOR_SIZE);
    return 0;
}

static struct block_device ramdisk_dev;

int ramdisk_init(void) {
    if (!frame_reserve_range(RAMDISK_BASE, RAMDISK_END)) {
        serial_puts("RAMDISK_INIT_FAIL\n");
        return BLKDEV_ERROR;
    }
    for (uint32_t addr = RAMDISK_BASE; addr < RAMDISK_END; addr += FRAME_SIZE) {
        paging_map_page(addr, addr, PAGE_PRESENT | PAGE_WRITABLE);
        if (!paging_is_mapped(addr)) {
            serial_puts("RAMDISK_INIT_FAIL\n");
            return BLKDEV_ERROR;
        }
    }

    memset(ramdisk_data, 0, RAMDISK_SIZE);
    
    // Initialize block device structure
    strncpy(ramdisk_dev.name, "ramdisk0", BLKDEV_MAX_NAME);
    ramdisk_dev.type = BLKDEV_TYPE_RAMDISK;
    ramdisk_dev.sector_count = RAMDISK_SECTORS;
    ramdisk_dev.sector_size = SECTOR_SIZE;
    ramdisk_dev.read_sector = ramdisk_read_sector;
    ramdisk_dev.write_sector = ramdisk_write_sector;
    ramdisk_dev.read_sectors = ramdisk_read_sectors;
    ramdisk_dev.write_sectors = ramdisk_write_sectors;
    ramdisk_dev.private_data = NULL;
    
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
```

---

## Testing Strategy

### Test 1: Basic Read/Write
```c
void test_ramdisk_basic(void) {
    struct block_device *dev = ramdisk_get_device();
    uint8_t write_buf[SECTOR_SIZE];
    uint8_t read_buf[SECTOR_SIZE];
    
    // Write pattern
    for (int i = 0; i < SECTOR_SIZE; i++) {
        write_buf[i] = i & 0xFF;
    }
    
    dev->write_sector(dev, 0, write_buf);
    dev->read_sector(dev, 0, read_buf);
    
    // Verify
    for (int i = 0; i < SECTOR_SIZE; i++) {
        if (read_buf[i] != write_buf[i]) {
            serial_puts("RAMDISK_BASIC_FAIL\n");
            return;
        }
    }
    
    serial_puts("RAMDISK_BASIC_OK\n");
}
```

### Test 2: Multiple Sectors
```c
void test_ramdisk_multi(void) {
    struct block_device *dev = ramdisk_get_device();
    uint8_t buf[SECTOR_SIZE * 4];
    
    // Write 4 sectors
    for (int i = 0; i < SECTOR_SIZE * 4; i++) {
        buf[i] = (i / SECTOR_SIZE) & 0xFF;
    }
    
    dev->write_sectors(dev, 100, 4, buf);
    
    memset(buf, 0, sizeof(buf));
    dev->read_sectors(dev, 100, 4, buf);
    
    // Verify
    for (int i = 0; i < SECTOR_SIZE * 4; i++) {
        if (buf[i] != ((i / SECTOR_SIZE) & 0xFF)) {
            serial_puts("RAMDISK_MULTI_FAIL\n");
            return;
        }
    }
    
    serial_puts("RAMDISK_MULTI_OK\n");
}
```

### Test 3: Bounds Check
```c
void test_ramdisk_bounds(void) {
    struct block_device *dev = ramdisk_get_device();
    uint8_t buf[SECTOR_SIZE];
    
    // Try to read beyond end
    int ret = dev->read_sector(dev, dev->sector_count, buf);
    if (ret == 0) {
        serial_puts("RAMDISK_BOUNDS_FAIL\n");
        return;
    }
    
    serial_puts("RAMDISK_BOUNDS_OK\n");
}
```

### Runtime Gate
```bash
make test-ramdisk
make test-deep
```

Required markers: `RAMDISK_INIT_OK`, `RAMDISK_DEVICE_OK`,
`RAMDISK_BASIC_OK`, `RAMDISK_MULTI_OK`, `RAMDISK_BOUNDS_OK`, `RAMDISK_OK`.

---

## Integration Steps

1. **Add header:** `include/blkdev.h`
2. **Add ramdisk implementation:** `kernel/ramdisk.c`
3. **Update Makefile:** Add `ramdisk.o` to kernel objects
4. **Call from kernel_main():** `ramdisk_init()`, halting on explicit init failure
5. **Add test:** `#ifdef ENABLE_RAMDISK_SELFTEST`

---

## Future Extensions

Once ramdisk works, the same block device interface can support:
- **ATA/IDE:** Real hard disk driver
- **VirtIO-blk:** Virtualized block device (faster in VMs)
- **USB storage:** USB mass storage driver

The VFS layer will work with any block device through this interface.

---

## Next Step

After ramdisk is working, we'll build the VFS layer on top of it.
