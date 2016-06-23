extern "C" {

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

}

#ifdef SIMULATOR

#define log_printf(...) fprintf(stderr, __VA_ARGS__)

#else

class Log {
    char *buffer;
    int size;
    int read_cursor;
    int write_cursor;

public:
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
