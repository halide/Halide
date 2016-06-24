#ifndef HALIDE_HEXAGON_REMOTE_LOG_H
#define HALIDE_HEXAGON_REMOTE_LOG_H

#ifdef SIMULATOR

// On the simulator, just use fprintf to implement log_printf.
#define log_printf(...) fprintf(stderr, __VA_ARGS__)

#else

// On device, we make a circular buffer that log_printf writes to, and
// we can read from in a remote RPC call.
class Log {
    char *buffer;
    int size;
    int read_cursor;
    int write_cursor;

public:
    // The size must be a power of 2.
    Log(int size) : buffer(NULL), size(size), read_cursor(0), write_cursor(0) {
        buffer = (char *)malloc(size);
    }

    ~Log() {
        free(buffer);
    }

    void write(const char *in, int in_size) {
        if (!buffer) return;

        for (int i = 0; i < in_size; i++, write_cursor++) {
            buffer[write_cursor & (size - 1)] = in[i];
        }
    }

    int read(char *out, int out_size) {
        if (out_size > write_cursor - read_cursor) {
            out_size = write_cursor - read_cursor;
        }
        for (int i = 0; i < out_size; i++, read_cursor++) {
            out[i] = buffer[read_cursor & (size - 1)];
        }
        return out_size;
    }
};

void log_printf(const char *fmt, ...);

#endif

#endif
