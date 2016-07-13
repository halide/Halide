#ifndef HALIDE_HEXAGON_REMOTE_LOG_H
#define HALIDE_HEXAGON_REMOTE_LOG_H

#ifdef SIMULATOR

// On the simulator, just use fprintf to implement log_printf.
#define log_printf(...) fprintf(stderr, __VA_ARGS__)

#else

#include <qurt.h>

// On device, we make a circular buffer that log_printf writes to, and
// we can read from in a remote RPC call.
class Log {
    char *buffer;
    int size;
    int read_cursor;
    int write_cursor;

    qurt_mutex_t lock;

public:
    // The size must be a power of 2.
    Log(int size) : buffer(NULL), size(size), read_cursor(0), write_cursor(0) {
        qurt_mutex_init(&lock);
        buffer = (char *)malloc(size);
    }

    ~Log() {
        free(buffer);
        qurt_mutex_destroy(&lock);
    }

    void write(const char *in, int in_size) {
        if (!buffer) return;

        qurt_mutex_lock(&lock);
        for (int i = 0; i < in_size; i++, write_cursor++) {
            buffer[write_cursor & (size - 1)] = in[i];
        }
        qurt_mutex_unlock(&lock);
    }

    int read(char *out, int out_size, char delim = 0) {
        qurt_mutex_lock(&lock);
        if (out_size > write_cursor - read_cursor) {
            out_size = write_cursor - read_cursor;
        }
        int i = 0;
        while (i < out_size) {
            char out_i = buffer[read_cursor++ & (size - 1)];
            out[i++] = out_i;
            if (out_i == delim) {
                break;
            }
        }
        qurt_mutex_unlock(&lock);
        return i;
    }
};

void log_printf(const char *fmt, ...);

#endif

#endif
