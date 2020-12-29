#include "log.h"

#include <qurt.h>
#include <stdio.h>
#include <stdlib.h>

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
    Log(int size)
        : buffer(NULL), size(size), read_cursor(0), write_cursor(0) {
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

// Use a 64 KB circular buffer to store log messages.
Log global_log(1024 * 64);

void log_printf(const char *fmt, ...) {
    char message[1024] = {
        0,
    };
    va_list ap;
    va_start(ap, fmt);
    int message_size = vsnprintf(message, sizeof(message) - 1, fmt, ap);
    va_end(ap);
    global_log.write(message, message_size);
}

extern "C" int halide_hexagon_remote_poll_log(char *out, int size, int *read_size) {
    // Read one line at a time.
    // Leave room for appending a null terminator.
    *read_size = global_log.read(out, size - 1, '\n');
    out[*read_size] = 0;
    return 0;
}
