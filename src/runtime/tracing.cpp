#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "scoped_spin_lock.h"

extern "C" {

typedef int32_t (*trace_fn)(void *, const halide_trace_event *);

}

namespace Halide { namespace Runtime { namespace Internal {

WEAK trace_fn halide_custom_trace = NULL;
WEAK int halide_trace_file = 0;
WEAK int halide_trace_file_lock = 0;
WEAK bool halide_trace_file_initialized = false;
WEAK bool halide_trace_file_internally_opened = false;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_set_custom_trace(trace_fn t) {
    halide_custom_trace = t;
}

WEAK void halide_set_trace_file(int fd) {
    halide_trace_file = fd;
    halide_trace_file_initialized = true;
}

extern int errno;

#define O_APPEND 1024
#define O_CREAT 64
#define O_WRONLY 1
WEAK int halide_get_trace_file(void *user_context) {
    // Prevent multiple threads both trying to initialize the trace
    // file at the same time.
    ScopedSpinLock lock(&halide_trace_file_lock);
    if (!halide_trace_file_initialized) {
        const char *trace_file_name = getenv("HL_TRACE_FILE");
        if (trace_file_name) {
            int fd = open(trace_file_name, O_APPEND | O_CREAT | O_WRONLY, 0644);
            halide_assert(user_context, (fd > 0) && "Failed to open trace file\n");
            halide_set_trace_file(fd);
            halide_trace_file_internally_opened = true;
        } else {
            halide_set_trace_file(0);
        }
    }
    return halide_trace_file;
}

WEAK int32_t halide_trace(void *user_context, const halide_trace_event *e) {

    static int32_t ids = 1;

    if (halide_custom_trace) {
        return (*halide_custom_trace)(user_context, e);
    } else {

        int32_t my_id = __sync_fetch_and_add(&ids, 1);

        // If we're dumping to a file, use a binary format
        int fd = halide_get_trace_file(user_context);
        if (fd > 0) {
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


            size_t written = write(fd, &buffer[0], total_bytes);
            halide_assert(user_context, written == total_bytes && "Can't write to trace file");

        } else {
            stringstream ss(user_context);

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

            ss << event_types[e->event] << " " << e->func << "." << e->value_index << "[";
            if (e->vector_width > 1) {
                ss << "<";
            }
            for (int i = 0; i < e->dimensions; i++) {
                if (i > 0) {
                    if ((e->vector_width > 1) && (i % e->vector_width) == 0) {
                        ss << ">, <";
                    } else {
                        ss << ", ";
                    }
                }
                ss << e->coordinates[i];
            }
            if (e->vector_width > 1) {
                ss << ">]";
            } else {
                ss << "]";
            }

            if (print_value) {
                if (e->vector_width > 1) {
                    ss << " = <";
                } else {
                    ss << " = ";
                }
                for (int i = 0; i < e->vector_width; i++) {
                    if (i > 0) {
                        ss << ", ";
                    }
                    if (e->type_code == 0) {
                        if (print_bits == 8) {
                            ss << ((int8_t *)(e->value))[i];
                        } else if (print_bits == 16) {
                            ss << ((int16_t *)(e->value))[i];
                        } else if (print_bits == 32) {
                            ss << ((int32_t *)(e->value))[i];
                        } else {
                            ss << ((int64_t *)(e->value))[i];
                        }
                    } else if (e->type_code == 1) {
                        if (print_bits == 8) {
                            ss << ((uint8_t *)(e->value))[i];
                        } else if (print_bits == 16) {
                            ss << ((uint16_t *)(e->value))[i];
                        } else if (print_bits == 32) {
                            ss << ((uint32_t *)(e->value))[i];
                        } else {
                            ss << ((uint64_t *)(e->value))[i];
                        }
                    } else if (e->type_code == 2) {
                        halide_assert(user_context, print_bits >= 32 && "Tracing a bad type");
                        if (print_bits == 32) {
                            ss << ((float *)(e->value))[i];
                        } else {
                            ss << ((double *)(e->value))[i];
                        }
                    } else if (e->type_code == 3) {
                        ss << ((void **)(e->value))[i];
                    }
                }
                if (e->vector_width > 1) {
                    ss << ">";
                }
            }
            ss << "\n";

            halide_print(user_context, ss.str());
        }

        return my_id;

    }
}

WEAK int halide_shutdown_trace() {
    if (halide_trace_file_internally_opened) {
        int ret = close(halide_trace_file);
        halide_trace_file = 0;
        halide_trace_file_initialized = false;
        halide_trace_file_internally_opened = false;
        return ret;
    } else {
        return 0;
    }
}

}
