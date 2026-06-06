#ifndef RAMDISK_H
#define RAMDISK_H

#include "blkdev.h"

int ramdisk_init(void);
struct block_device *ramdisk_get_device(void);

#endif
