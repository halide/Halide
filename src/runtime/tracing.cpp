#include "HalideRuntime.h"

// Forward declared so that we can use this code on ios too
extern "C" {

extern char *getenv(const char *);
extern void *fopen(const char *path, const char *mode);
extern size_t fwrite(const void *ptr, size_t size, size_t n, void *file);
extern int snprintf(char *str, size_t size, const char *format, ...);
extern int fclose(void *f);

typedef void (*trace_fn)(const char *, halide_trace_event_t,
                         int32_t, int32_t, int32_t, int32_t,
                         const void *, int32_t, const int32_t *);
WEAK trace_fn halide_custom_trace = NULL;

WEAK void halide_set_custom_trace(trace_fn t) {
    halide_custom_trace = t;
}

WEAK void *halide_trace_file = NULL;

WEAK void halide_trace(const char *func, halide_trace_event_t event,
                       int32_t type_code, int32_t bits, int32_t width, int32_t value_idx, void *value,
                       int32_t num_int_args, const int32_t *int_args) {
    if (halide_custom_trace) {
        (*halide_custom_trace)(func, event, type_code, bits, width, value_idx, value, num_int_args, int_args);
    } else {
        static bool initialized = false;

        if (!initialized) {
            const char *trace_file_name = getenv("HL_TRACE_FILE");
            initialized = true;
            if (trace_file_name) {
                halide_trace_file = fopen(trace_file_name, "wb");
                halide_assert(halide_trace_file && "Failed to open trace file\n");
            }
        }


        // If we're dumping to a file, use a binary format
        if (halide_trace_file) {
            // A 32-byte header. The first 6 bytes are metadata, then the rest is a zero-terminated string.
            uint8_t clamped_width = width < 256 ? width : 255;
            uint8_t clamped_num_int_args = num_int_args < 256 ? num_int_args : 255;

            // Upgrade the bit count to a power of two, because that's
            // how it will be stored on the stack.
            int bytes = 1;
            while (bytes < (bits*8)) bytes <<= 1;

            // Compute the size of each portion of the tracing packet
            size_t header_bytes = 32;
            size_t value_bytes = clamped_width * bytes;
            size_t int_arg_bytes = clamped_num_int_args * sizeof(int32_t);
            size_t total_bytes = header_bytes + value_bytes + int_arg_bytes;
            uint8_t buffer[4096];
            halide_assert(total_bytes <= 4096 && "Tracing packet too large");

            buffer[0] = event;
            buffer[1] = type_code;
            buffer[2] = bits;
            buffer[3] = clamped_width;
            buffer[4] = value_idx;
            buffer[5] = clamped_num_int_args;

            // Use up to 25 bytes for the function name
            int i = 6;
            for (; i < header_bytes-1; i++) {
                buffer[i] = func[i-6];
                if (buffer[i] == 0) break;
            }
            // Fill the rest with zeros
            for (; i < header_bytes; i++) {
                buffer[i] = 0;
            }

            // Next comes the value
            for (size_t i = 0; i < value_bytes; i++) {
                buffer[header_bytes + i] = ((uint8_t *)value)[i];
            }

            // Then the int args
            for (size_t i = 0; i < int_arg_bytes; i++) {
                buffer[header_bytes + value_bytes + i] = ((uint8_t *)int_args)[i];
            }


            size_t written = fwrite(&buffer[0], 1, total_bytes, halide_trace_file);
            halide_assert(written == total_bytes && "Can't write to trace file");

        } else {
            char buf[256];
            char *buf_ptr = &buf[0];
            char *buf_end = &buf[255];

            // Round up bits to 8, 16, 32, or 64
            int print_bits = 8;
            while (print_bits < bits) print_bits <<= 1;
            halide_assert(print_bits <= 64 && "Tracing bad type");

            // Otherwise, use halide_printf and a plain-text format
            const char *event_types[] = {"Load",
                                         "Store",
                                         "Begin realization",
                                         "End realization",
                                         "Produce",
                                         "Update",
                                         "Consume",
                                         "End consume"};

            if (buf_ptr < buf_end) {
                buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%s %s.%d[", event_types[event], func, value_idx);
            }
            for (int i = 0; i < num_int_args && buf_ptr < buf_end; i++) {
                if (i > 0) {
                    buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, ", %d", int_args[i]);
                } else {
                    buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", int_args[i]);
                }
            }
            if (buf_ptr < buf_end) {
                buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "] = (");
            }

            for (int i = 0; i < width && buf_ptr < buf_end; i++) {
                if (i > 0) {
                    buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, ", ");
                }
                if (type_code == 0) {
                    if (print_bits == 8) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", ((int8_t *)(value))[i]);
                    } else if (print_bits == 16) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", ((int16_t *)(value))[i]);
                    } else if (print_bits == 32) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", ((int32_t *)(value))[i]);
                    } else {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", (int32_t)((int64_t *)(value))[i]);
                    }
                } else if (type_code == 1) {
                    if (print_bits == 8) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%u", ((uint8_t *)(value))[i]);
                        if (buf_ptr > buf_end) buf_ptr = buf_end;
                    } else if (print_bits == 16) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%u", ((uint16_t *)(value))[i]);
                    } else if (print_bits == 32) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%u", ((uint32_t *)(value))[i]);
                    } else {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%u", (uint32_t)((uint64_t *)(value))[i]);
                    }
                } else if (type_code == 2) {
                    halide_assert(print_bits >= 32 && "Tracing a bad type");
                    if (print_bits == 32) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%f", ((float *)(value))[i]);
                    } else {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%f", ((double *)(value))[i]);
                    }
                } else if (type_code == 3) {
                    buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%p", ((void **)(value))[i]);
                }
            }

            if (buf_ptr < buf_end) {
                buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, ")");
            }

            halide_printf("%s\n", buf);
        }
    }
}

WEAK void halide_shutdown_trace() {
    if (halide_trace_file) {
        fclose(halide_trace_file);
        halide_trace_file = NULL;
    }
}

}
