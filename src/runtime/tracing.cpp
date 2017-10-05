#include "HalideRuntime.h"
#include "printer.h"
#include "scoped_spin_lock.h"

extern "C" {

typedef int32_t (*trace_fn)(void *, const halide_trace_event_t *);

}

namespace Halide { namespace Runtime { namespace Internal {

// A spinlock that allows for shared and exclusive access. It's
// equivalent to a reader-writer lock, but in my case the "readers"
// will actually be writing simultaneously to the trace buffer, so
// that's a bad name.
class SharedExclusiveSpinLock {
    uint32_t lock;
    const static uint32_t exclusive_held_mask = 0x80000000;
    const static uint32_t exclusive_waiting_mask = 0x40000000;
    const static uint32_t shared_mask = 0x3fffffff;

public:
    void acquire_shared() {
        while (1) {
            uint32_t x = lock & shared_mask;
            if (__sync_bool_compare_and_swap(&lock, x, x + 1)) {
                return;
            }
        }
    }

    void release_shared() {
        __sync_fetch_and_sub(&lock, 1);
    }

    void acquire_exclusive() {
        while (1) {
            __sync_fetch_and_or(&lock, exclusive_waiting_mask);
            if (__sync_bool_compare_and_swap(&lock, exclusive_waiting_mask, exclusive_held_mask)) {
                return;
            }
        }
    }

    void release_exclusive() {
        __sync_fetch_and_xor(&lock, exclusive_held_mask);
    }

    SharedExclusiveSpinLock() : lock(0) {}
};

class TraceBuffer {
    SharedExclusiveSpinLock lock;
    uint32_t cursor;
    uint8_t buf[1024*1024];

public:

    // Attempt to atomically acquire space in the buffer to write a
    // packet. Returns NULL if the buffer was full.
    halide_trace_packet_t *try_acquire_packet(uint32_t size) {
        lock.acquire_shared();
        uint32_t my_cursor = __sync_fetch_and_add(&cursor, size);
        if (my_cursor + size > sizeof(buf)) {
            __sync_fetch_and_sub(&cursor, size);
            lock.release_shared();
            return NULL;
        } else {
            return (halide_trace_packet_t *)(buf + my_cursor);
        }
    }

    // Wait for all writers to finish with their packets, stall any
    // new writers, and flush the buffer to the fd.
    void flush(void *user_context, int fd) {
        lock.acquire_exclusive();
        bool success = true;
        if (cursor) {
            success = (cursor == (uint32_t)write(fd, buf, cursor));
            cursor = 0;
        }
        lock.release_exclusive();
        halide_assert(user_context, success && "Could not write to trace file");
    }

    // Acquire and return a packet's worth of space in the trace
    // buffer, flushing the trace buffer to the given fd to make space
    // if necessary.
    halide_trace_packet_t *acquire_packet(void *user_context, int fd, uint32_t size) {
        halide_trace_packet_t *packet = NULL;
        while (!(packet = try_acquire_packet(size))) {
            // Couldn't acquire space to write a packet. Flush and try again.
            flush(user_context, fd);
        }
        return packet;
    }

    // Release a packet, allowing it to be written out with flush
    void release_packet(halide_trace_packet_t *) {
        // Need a memory barrier to guarantee all the writes are done.
        __sync_synchronize();
        lock.release_shared();
    }

    TraceBuffer() : cursor(0) {}
} trace_buffer;


WEAK int halide_trace_file = -1; // -1 indicates uninitialized
WEAK int halide_trace_file_lock = 0;
WEAK bool halide_trace_file_initialized = false;
WEAK void *halide_trace_file_internally_opened = NULL;

}}}

extern "C" {

WEAK int32_t halide_default_trace(void *user_context, const halide_trace_event_t *e) {
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

        // Claim some space to write to in the trace buffer
        halide_trace_packet_t *packet = trace_buffer.acquire_packet(user_context, fd, total_size);

        if (total_size > 4096) {
            print(NULL) << total_size << "\n";
        }

        // Write a packet into it
        packet->size = total_size;
        packet->id = my_id;
        packet->type = e->type;
        packet->event = e->event;
        packet->parent_id = e->parent_id;
        packet->value_index = e->value_index;
        packet->dimensions = e->dimensions;
        if (e->coordinates) {
            memcpy((void *)packet->coordinates(), e->coordinates, coords_bytes);
        }
        if (e->value) {
            memcpy((void *)packet->value(), e->value, value_bytes);
        }
        memcpy((void *)packet->func(), e->func, name_bytes);

        // Release it
        trace_buffer.release_packet(packet);

        // We should also flush the trace buffer if we hit an event
        // that might be the end of the trace.
        if (e->event == halide_trace_end_pipeline) {
            trace_buffer.flush(user_context, fd);
        }

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

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

WEAK trace_fn halide_custom_trace = halide_default_trace;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK trace_fn halide_set_custom_trace(trace_fn t) {
    trace_fn result = halide_custom_trace;
    halide_custom_trace = t;
    return result;
}

WEAK void halide_set_trace_file(int fd) {
    halide_trace_file = fd;
}

extern int errno;

WEAK int halide_get_trace_file(void *user_context) {
    if (halide_trace_file >= 0) {
        return halide_trace_file;
    }
    {
        // Prevent multiple threads both trying to initialize the trace
        // file at the same time.
        ScopedSpinLock lock(&halide_trace_file_lock);
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
