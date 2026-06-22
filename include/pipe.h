#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#define MAX_PIPES 8
#define PIPE_CAPACITY 4096u
#define PIPE_BUF 512u

#define PIPE_SUCCESS 0
#define PIPE_EBADF -1
#define PIPE_WOULD_BLOCK -2
#define PIPE_BROKEN -3

struct pipe {
    uint8_t buffer[PIPE_CAPACITY];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint32_t readers;
    uint32_t writers;
    int used;
};

void pipe_init(void);
int pipe_create(int *read_handle, int *write_handle);
int pipe_read(int handle, void *buffer, uint32_t count);
int pipe_write(int handle, const void *buffer, uint32_t count);
int pipe_close(int handle);
int pipe_retain(int handle);
int pipe_handle_index(int handle);
int pipe_handle_is_read(int handle);

#endif
