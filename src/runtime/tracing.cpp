#include "mini_stdint.h"
#include "HalideRuntime.h"

extern "C" {

extern char *getenv(const char *);
extern void *fopen(const char *path, const char *mode);
extern size_t fwrite(const void *ptr, size_t size, size_t n, void *file);
extern int snprintf(char *str, size_t size, const char *format, ...);
extern int fclose(void *f);

typedef int32_t (*trace_fn)(void *, const halide_trace_event *);

WEAK trace_fn halide_custom_trace = NULL;

WEAK void halide_set_custom_trace(trace_fn t) {
    halide_custom_trace = t;
}

WEAK void *halide_trace_file = NULL;
WEAK bool halide_trace_initialized = false;
WEAK int32_t halide_trace(void *user_context, const halide_trace_event *e) {

    static int32_t ids = 1;

    if (halide_custom_trace) {
        return (*halide_custom_trace)(user_context, e);
    } else {

        int32_t my_id = __sync_fetch_and_add(&ids, 1);

        if (!halide_trace_initialized) {
            const char *trace_file_name = getenv("HL_TRACE_FILE");
            halide_trace_initialized = true;
            if (trace_file_name) {
                halide_trace_file = fopen(trace_file_name, "ab");
                halide_assert(user_context, halide_trace_file && "Failed to open trace file\n");
            }
        }

        // If we're dumping to a file, use a binary format
        if (halide_trace_file) {
            // A 32-byte header. The first 6 bytes are metadata, then the rest is a zero-terminated string.
            uint8_t clamped_width = e->vector_width < 256 ? e->vector_width : 255;
            uint8_t clamped_dimensions = e->dimensions < 256 ? e->dimensions : 255;

            // Upgrade the bit count to a power of two, because that's
            // how it will be stored on the stack.
            int bytes = 1;
            while (bytes*8 < e->bits) bytes <<= 1;

            // Compute the size of each portion of the tracing packet
            size_t header_bytes = 32;
            size_t value_bytes = clamped_width * bytes;
            size_t int_arg_bytes = clamped_dimensions * sizeof(int32_t);
            size_t total_bytes = header_bytes + value_bytes + int_arg_bytes;
            uint8_t buffer[4096];
            halide_assert(user_context, total_bytes <= 4096 && "Tracing packet too large");

            ((int32_t *)buffer)[0] = my_id;
            ((int32_t *)buffer)[1] = e->parent_id;
            buffer[8] = e->event;
            buffer[9] = e->type_code;
            buffer[10] = e->bits;
            buffer[11] = clamped_width;
            buffer[12] = e->value_index;
            buffer[13] = clamped_dimensions;

            // Use up to 17 bytes for the function name
            int i = 14;
            for (; i < header_bytes-1; i++) {
                buffer[i] = e->func[i-14];
                if (buffer[i] == 0) break;
            }
            // Fill the rest with zeros
            for (; i < header_bytes; i++) {
                buffer[i] = 0;
            }

            // Next comes the value
            for (size_t i = 0; i < value_bytes; i++) {
                buffer[header_bytes + i] = ((uint8_t *)(e->value))[i];
            }

            // Then the int args
            for (size_t i = 0; i < int_arg_bytes; i++) {
                buffer[header_bytes + value_bytes + i] = ((uint8_t *)(e->coordinates))[i];
            }


            size_t written = fwrite(&buffer[0], 1, total_bytes, halide_trace_file);
            halide_assert(user_context, written == total_bytes && "Can't write to trace file");

        } else {
            char buf[256];
            char *buf_ptr = &buf[0];
            char *buf_end = &buf[255];

            // Round up bits to 8, 16, 32, or 64
            int print_bits = 8;
            while (print_bits < e->bits) print_bits <<= 1;
            halide_assert(user_context, print_bits <= 64 && "Tracing bad type");

            // Otherwise, use halide_printf and a plain-text format
            const char *event_types[] = {"Load",
                                         "Store",
                                         "Begin realization",
                                         "End realization",
                                         "Produce",
                                         "Update",
                                         "Consume",
                                         "End consume"};

            // Only print out the value on stores and loads.
            bool print_value = (e->event < 2);

            if (buf_ptr < buf_end) {
                buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%s %s.%d[",
                                    event_types[e->event], e->func, e->value_index);
            }
            if (e->vector_width > 1) {
                buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "<");
            }
            for (int i = 0; i < e->dimensions && buf_ptr < buf_end; i++) {
                if (i > 0) {
                    if ((e->vector_width > 1) && (i % e->vector_width) == 0) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, ">, <");
                    } else {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, ", ");
                    }
                }
                buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", e->coordinates[i]);
            }
            if (buf_ptr < buf_end) {
                if (e->vector_width > 1) {
                    buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, ">]");
                } else {
                    buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "]");
                }
            }

            if (print_value) {
                if (buf_ptr < buf_end) {
                    if (e->vector_width > 1) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, " = <");
                    } else {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, " = ");
                    }
                }
                for (int i = 0; i < e->vector_width && buf_ptr < buf_end; i++) {
                    if (i > 0) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, ", ");
                    }
                    if (e->type_code == 0) {
                        if (print_bits == 8) {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", ((int8_t *)(e->value))[i]);
                        } else if (print_bits == 16) {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", ((int16_t *)(e->value))[i]);
                        } else if (print_bits == 32) {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", ((int32_t *)(e->value))[i]);
                        } else {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%d", (int32_t)((int64_t *)(e->value))[i]);
                        }
                    } else if (e->type_code == 1) {
                        if (print_bits == 8) {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%u", ((uint8_t *)(e->value))[i]);
                            if (buf_ptr > buf_end) buf_ptr = buf_end;
                        } else if (print_bits == 16) {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%u", ((uint16_t *)(e->value))[i]);
                        } else if (print_bits == 32) {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%u", ((uint32_t *)(e->value))[i]);
                        } else {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%u", (uint32_t)((uint64_t *)(e->value))[i]);
                        }
                    } else if (e->type_code == 2) {
                        halide_assert(user_context, print_bits >= 32 && "Tracing a bad type");
                        if (print_bits == 32) {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%f", ((float *)(e->value))[i]);
                        } else {
                            buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%f", ((double *)(e->value))[i]);
                        }
                    } else if (e->type_code == 3) {
                        buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, "%p", ((void **)(e->value))[i]);
                    }
                }
                if (e->vector_width > 1 && buf_ptr < buf_end) {
                    buf_ptr += snprintf(buf_ptr, buf_end - buf_ptr, ">");
                }
            }

            halide_printf(user_context, "%s\n", buf);
        }

        return my_id;

    }
}

WEAK int halide_shutdown_trace() {
    if (halide_trace_file) {
        int ret = fclose(halide_trace_file);
        halide_trace_file = NULL;
        halide_trace_initialized = false;
        return ret;
    } else {
        return 0;
    }
}

}
