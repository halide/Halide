#include "HalideRuntime.h"
#include "printer.h"
#include "scoped_spin_lock.h"

extern "C" {

typedef int32_t (*trace_fn)(void *, const halide_trace_event_t *);

}

namespace Halide { namespace Runtime { namespace Internal {

WEAK int halide_trace_file = 0;
WEAK int halide_trace_file_lock = 0;
WEAK bool halide_trace_file_initialized = false;
WEAK void *halide_trace_file_internally_opened = NULL;

WEAK int32_t default_trace(void *user_context, const halide_trace_event_t *e) {
    static int32_t ids = 1;

    int32_t my_id = __sync_fetch_and_add(&ids, 1);

    // If we're dumping to a file, use a binary format
    int fd = halide_get_trace_file(user_context);
    if (fd > 0) {
        // Compute the total packet size
        uint32_t value_bytes = (uint32_t)(e->type.lanes * e->type.bytes());
        uint32_t header_bytes = (uint32_t)sizeof(halide_trace_packet_t);
        uint32_t coords_bytes = e->dimensions * (uint32_t)sizeof(int32_t);
        uint32_t name_bytes = strlen(e->func) + 1;
        uint32_t total_size_without_padding = header_bytes + value_bytes + coords_bytes + name_bytes;
        uint32_t total_size = (total_size_without_padding + 3) & ~3;
        uint32_t padding_bytes = total_size - total_size_without_padding;

        // The packet header
        halide_trace_packet_t header;
        header.size = total_size;
        header.id = my_id;
        header.type = e->type;
        header.event = e->event;
        header.parent_id = e->parent_id;
        header.value_index = e->value_index;
        header.dimensions = e->dimensions;

        size_t written = 0;
        {
            ScopedSpinLock lock(&halide_trace_file_lock);
            written += write(fd, &header, sizeof(header));
            if (e->coordinates) {
                written += write(fd, e->coordinates, coords_bytes);
            }
            if (e->value) {
                written += write(fd, e->value, value_bytes);
            }
            written += write(fd, e->func, name_bytes);
            uint32_t zero = 0;
            written += write(fd, &zero, padding_bytes);
        }
        halide_assert(user_context, written == total_size && "Can't write to trace file");

    } else {
        uint8_t buffer[4096];
        Printer<StringStreamPrinter, sizeof(buffer)> ss(user_context, (char *)buffer);

        // Round up bits to 8, 16, 32, or 64
        int print_bits = 8;
        while (print_bits < e->type.bits) print_bits <<= 1;
        halide_assert(user_context, print_bits <= 64 && "Tracing bad type");

        // Otherwise, use halide_print and a plain-text format
        const char *event_types[] = {"Load",
                                     "Store",
                                     "Begin realization",
                                     "End realization",
                                     "Produce",
                                     "End produce",
                                     "Consume",
                                     "End consume",
                                     "Begin pipeline",
                                     "End pipeline"};

        // Only print out the value on stores and loads.
        bool print_value = (e->event < 2);

        ss << event_types[e->event] << " " << e->func << "." << e->value_index << "(";
        if (e->type.lanes > 1) {
            ss << "<";
        }
        for (int i = 0; i < e->dimensions; i++) {
            if (i > 0) {
                if ((e->type.lanes > 1) && (i % e->type.lanes) == 0) {
                    ss << ">, <";
                } else {
                    ss << ", ";
                }
            }
            ss << e->coordinates[i];
        }
        if (e->type.lanes > 1) {
            ss << ">)";
        } else {
            ss << ")";
        }

        if (print_value) {
            if (e->type.lanes > 1) {
                ss << " = <";
            } else {
                ss << " = ";
            }
            for (int i = 0; i < e->type.lanes; i++) {
                if (i > 0) {
                    ss << ", ";
                }
                if (e->type.code == 0) {
                    if (print_bits == 8) {
                        ss << ((int8_t *)(e->value))[i];
                    } else if (print_bits == 16) {
                        ss << ((int16_t *)(e->value))[i];
                    } else if (print_bits == 32) {
                        ss << ((int32_t *)(e->value))[i];
                    } else {
                        ss << ((int64_t *)(e->value))[i];
                    }
                } else if (e->type.code == 1) {
                    if (print_bits == 8) {
                        ss << ((uint8_t *)(e->value))[i];
                    } else if (print_bits == 16) {
                        ss << ((uint16_t *)(e->value))[i];
                    } else if (print_bits == 32) {
                        ss << ((uint32_t *)(e->value))[i];
                    } else {
                        ss << ((uint64_t *)(e->value))[i];
                    }
                } else if (e->type.code == 2) {
                    halide_assert(user_context, print_bits >= 16 && "Tracing a bad type");
                    if (print_bits == 32) {
                        ss << ((float *)(e->value))[i];
                    } else if (print_bits == 16) {
                        ss.write_float16_from_bits( ((uint16_t *)(e->value))[i]);
                    } else {
                        ss << ((double *)(e->value))[i];
                    }
                } else if (e->type.code == 3) {
                    ss << ((void **)(e->value))[i];
                }
            }
            if (e->type.lanes > 1) {
                ss << ">";
            }
        }
        ss << "\n";
        ss.msan_annotate_is_initialized();

        {
            ScopedSpinLock lock(&halide_trace_file_lock);
            halide_print(user_context, (const char *)buffer);
        }
    }

    return my_id;
}

WEAK trace_fn halide_custom_trace = default_trace;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK trace_fn halide_set_custom_trace(trace_fn t) {
    trace_fn result = halide_custom_trace;
    halide_custom_trace = t;
    return result;
}

WEAK void halide_set_trace_file(int fd) {
    halide_trace_file = fd;
    halide_trace_file_initialized = true;
}

extern int errno;

WEAK int halide_get_trace_file(void *user_context) {
    // Prevent multiple threads both trying to initialize the trace
    // file at the same time.
    ScopedSpinLock lock(&halide_trace_file_lock);
    if (!halide_trace_file_initialized) {
        const char *trace_file_name = getenv("HL_TRACE_FILE");
        if (trace_file_name) {
            void *file = fopen(trace_file_name, "ab");
            halide_assert(user_context, file && "Failed to open trace file\n");
            halide_set_trace_file(fileno(file));
            halide_trace_file_internally_opened = file;
        } else {
            halide_set_trace_file(0);
        }
    }
    return halide_trace_file;
}

WEAK int32_t halide_trace(void *user_context, const halide_trace_event_t *e) {
    return (*halide_custom_trace)(user_context, e);
}

WEAK int halide_shutdown_trace() {
    if (halide_trace_file_internally_opened) {
        int ret = fclose(halide_trace_file_internally_opened);
        halide_trace_file = 0;
        halide_trace_file_initialized = false;
        halide_trace_file_internally_opened = NULL;
        return ret;
    } else {
        return 0;
    }
}

namespace {
__attribute__((destructor))
WEAK void halide_trace_cleanup() {
    halide_shutdown_trace();
}
}

// A wrapper for halide_trace called by the pipeline. Halide Stmt IR
// has a hard time packing structs itself.
WEAK int halide_trace_helper(void *user_context,
                             const char *func,
                             void *value, int *coords,
                             int type_code, int type_bits, int type_lanes,
                             int code,
                             int parent_id, int value_index, int dimensions) {
    halide_trace_event_t event;
    event.func = func;
    event.coordinates = coords;
    event.value = value;
    event.type.code = (halide_type_code_t)type_code;
    event.type.bits = (uint8_t)type_bits;
    event.type.lanes = (uint16_t)type_lanes;
    event.event = (halide_trace_event_code_t)code;
    event.parent_id = parent_id;
    event.value_index = value_index;
    if (event.type.lanes > 1) {
        dimensions *= event.type.lanes;
    }
    event.dimensions = dimensions;
    return halide_trace(user_context, &event);
}

}
