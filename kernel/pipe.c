#include "pipe.h"
#include "process.h"
#include "string.h"
#include <stdint.h>

#define PIPE_HANDLE_BASE 256
#define PIPE_READ_END 0
#define PIPE_WRITE_END 1

static struct pipe pipes[MAX_PIPES];

void pipe_init(void) {
    memset(pipes, 0, sizeof(pipes));
}

int pipe_handle_index(int handle) {
    int index;

    if (handle < PIPE_HANDLE_BASE) {
        return -1;
    }
    index = (handle - PIPE_HANDLE_BASE) / 2;
    if (index < 0 || index >= MAX_PIPES || !pipes[index].used) {
        return -1;
    }
    return index;
}

int pipe_handle_is_read(int handle) {
    return handle >= PIPE_HANDLE_BASE &&
           ((handle - PIPE_HANDLE_BASE) % 2) == PIPE_READ_END;
}

int pipe_create(int *read_handle, int *write_handle) {
    if (!read_handle || !write_handle) {
        return PIPE_EBADF;
    }
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].used) {
            memset(&pipes[i], 0, sizeof(pipes[i]));
            pipes[i].used = 1;
            pipes[i].readers = 1;
            pipes[i].writers = 1;
            *read_handle = PIPE_HANDLE_BASE + i * 2 + PIPE_READ_END;
            *write_handle = PIPE_HANDLE_BASE + i * 2 + PIPE_WRITE_END;
            return PIPE_SUCCESS;
        }
    }
    return PIPE_EBADF;
}

int pipe_read(int handle, void *buffer, uint32_t count) {
    int index = pipe_handle_index(handle);
    struct pipe *pipe;
    uint32_t copied;

    if (index < 0 || !pipe_handle_is_read(handle) || (!buffer && count > 0)) {
        return PIPE_EBADF;
    }
    if (count == 0) {
        return 0;
    }
    pipe = &pipes[index];
    if (pipe->count == 0) {
        return pipe->writers == 0 ? 0 : PIPE_WOULD_BLOCK;
    }

    copied = count < pipe->count ? count : pipe->count;
    for (uint32_t i = 0; i < copied; i++) {
        ((uint8_t *)buffer)[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_CAPACITY;
    }
    pipe->count -= copied;
    process_wake_pipe_waiter(index, BLOCK_PIPE_WRITE);
    return (int)copied;
}

int pipe_write(int handle, const void *buffer, uint32_t count) {
    int index = pipe_handle_index(handle);
    struct pipe *pipe;
    uint32_t space;
    uint32_t copied;

    if (index < 0 || pipe_handle_is_read(handle) || (!buffer && count > 0)) {
        return PIPE_EBADF;
    }
    if (count == 0) {
        return 0;
    }
    pipe = &pipes[index];
    if (pipe->readers == 0) {
        return PIPE_BROKEN;
    }

    space = PIPE_CAPACITY - pipe->count;
    if (space == 0 || (count <= PIPE_BUF && space < count)) {
        return PIPE_WOULD_BLOCK;
    }
    copied = count < space ? count : space;
    for (uint32_t i = 0; i < copied; i++) {
        pipe->buffer[pipe->write_pos] = ((const uint8_t *)buffer)[i];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_CAPACITY;
    }
    pipe->count += copied;
    process_wake_pipe_waiter(index, BLOCK_PIPE_READ);
    return (int)copied;
}

int pipe_close(int handle) {
    int index = pipe_handle_index(handle);
    struct pipe *pipe;

    if (index < 0) {
        return PIPE_EBADF;
    }
    pipe = &pipes[index];
    if (pipe_handle_is_read(handle)) {
        if (pipe->readers == 0) {
            return PIPE_EBADF;
        }
        pipe->readers--;
        if (pipe->readers == 0) {
            process_wake_pipe_waiter(index, BLOCK_PIPE_WRITE);
        }
    } else {
        if (pipe->writers == 0) {
            return PIPE_EBADF;
        }
        pipe->writers--;
        if (pipe->writers == 0) {
            process_wake_pipe_waiter(index, BLOCK_PIPE_READ);
        }
    }
    if (pipe->readers == 0 && pipe->writers == 0) {
        memset(pipe, 0, sizeof(*pipe));
    }
    return PIPE_SUCCESS;
}

int pipe_retain(int handle) {
    int index = pipe_handle_index(handle);
    struct pipe *pipe;

    if (index < 0) {
        return PIPE_EBADF;
    }
    pipe = &pipes[index];
    if (pipe_handle_is_read(handle)) {
        if (pipe->readers == UINT32_MAX) {
            return PIPE_EBADF;
        }
        pipe->readers++;
    } else {
        if (pipe->writers == UINT32_MAX) {
            return PIPE_EBADF;
        }
        pipe->writers++;
    }
    return PIPE_SUCCESS;
}
