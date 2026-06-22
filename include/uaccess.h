#ifndef UACCESS_H
#define UACCESS_H

#include <stdint.h>

#define UACCESS_SUCCESS 0
#define UACCESS_EFAULT -1
#define UACCESS_ENOMEM -2

int uaccess_range_valid(uint32_t address, uint32_t size);
int uaccess_readable(uint32_t address, uint32_t size);
int uaccess_prepare_write(uint32_t address, uint32_t size);
int copy_from_user(void *dest, uint32_t source, uint32_t size);
int copy_to_user(uint32_t dest, const void *source, uint32_t size);
int copy_string_from_user(char *dest, uint32_t source, uint32_t max_size);

#endif
