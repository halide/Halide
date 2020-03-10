#include "HalideRuntime.h"
#include "printer.h"
#include "scoped_spin_lock.h"

extern "C" {

typedef int32_t (*trace_fn)(void *, const halide_trace_event_t *);
}

namespace Halide {
namespace Runtime {
namespace Internal {

// A spinlock that allows for shared and exclusive access. It's
// equivalent to a reader-writer lock, but in my case the "readers"
// will actually be writing simultaneously to the trace buffer, so
// that's a bad name. We use the __sync primitives used elsewhere in
// the runtime for atomic work. They are well supported by clang.
class SharedExclusiveSpinLock {
    volatile uint32_t lock;

    // Covers a single bit indicating one owner has exclusive
    // access. The waiting bit can be set while the exclusive bit is
    // set, but the bits masked by shared_mask must be zero while this
    // bit is set.
    const static uint32_t exclusive_held_mask = 0x80000000;

    // Set to indicate a thread needs to acquire exclusive
    // access. Other fields of the lock may be set, but no shared
    // access request will proceed while this bit is set.
    const static uint32_t exclusive_waiting_mask = 0x40000000;

    // Count of threads currently holding shared access. Must be zero
    // if the exclusive bit is set. Cannot increase if the waiting bit
    // is set.
    const static uint32_t shared_mask = 0x3fffffff;

public:
    __attribute__((always_inline)) void acquire_shared() {
        while (1) {
            uint32_t x = lock & shared_mask;
            if (__sync_bool_compare_and_swap(&lock, x, x + 1)) {
                return;
            }
        }
    }

    __attribute__((always_inline)) void release_shared() {
        __sync_fetch_and_sub(&lock, 1);
    }

    __attribute__((always_inline)) void acquire_exclusive() {
        while (1) {
            // If multiple threads are trying to acquire exclusive
            // ownership, we may need to rerequest exclusive waiting
            // while we spin, as it gets unset whenever a thread
            // acquires exclusive ownership.
            __sync_fetch_and_or(&lock, exclusive_waiting_mask);
            if (__sync_bool_compare_and_swap(&lock, exclusive_waiting_mask, exclusive_held_mask)) {
                return;
            }
        }
    }

    __attribute__((always_inline)) void release_exclusive() {
        __sync_fetch_and_and(&lock, ~exclusive_held_mask);
    }

    __attribute__((always_inline)) void init() {
        lock = 0;
    }

    SharedExclusiveSpinLock()
        : lock(0) {
    }
};

const static int buffer_size = 1024 * 1024;

class TraceBuffer {
    SharedExclusiveSpinLock lock;
    uint32_t cursor, overage;
    uint8_t buf[buffer_size];

    // Attempt to atomically acquire space in the buffer to write a
    // packet. Returns NULL if the buffer was full.
    __attribute__((always_inline)) halide_trace_packet_t *try_acquire_packet(void *user_context, uint32_t size) {
        lock.acquire_shared();
        halide_assert(user_context, size <= buffer_size);
        uint32_t my_cursor = __sync_fetch_and_add(&cursor, size);
        if (my_cursor + size > sizeof(buf)) {
            // Don't try to back it out: instead, just allow this request to fail
            // (along with all subsequent requests) and record the 'overage'
            // that was added and should be ignored; then, in the next flush,
            // remove the overage.
            __sync_fetch_and_add(&overage, size);
            lock.release_shared();
            return NULL;
        } else {
            return (halide_trace_packet_t *)(buf + my_cursor);
        }
    }

public:
    // Wait for all writers to finish with their packets, stall any
    // new writers, and flush the buffer to the fd.
    __attribute__((always_inline)) void flush(void *user_context, int fd) {
        lock.acquire_exclusive();
        bool success = true;
        if (cursor) {
            cursor -= overage;
            success = (cursor == (uint32_t)write(fd, buf, cursor));
            cursor = 0;
            overage = 0;
        }
        lock.release_exclusive();
        halide_assert(user_context, success && "Could not write to trace file");
    }

    // Acquire and return a packet's worth of space in the trace
    // buffer, flushing the trace buffer to the given fd to make space
    // if necessary. The region acquired is protected from other
    // threads writing or reading to it, so it must be released before
    // a flush can occur.
    __attribute__((always_inline)) halide_trace_packet_t *acquire_packet(void *user_context, int fd, uint32_t size) {
        halide_trace_packet_t *packet = NULL;
        while (!(packet = try_acquire_packet(user_context, size))) {
            // Couldn't acquire space to write a packet. Flush and try again.
            flush(user_context, fd);
        }
        return packet;
    }

    // Release a packet, allowing it to be written out with flush
    __attribute__((always_inline)) void release_packet(halide_trace_packet_t *) {
        // Need a memory barrier to guarantee all the writes are done.
        __sync_synchronize();
        lock.release_shared();
    }

    __attribute__((always_inline)) void init() {
        cursor = 0;
        overage = 0;
        lock.init();
    }

    TraceBuffer()
        : cursor(0), overage(0) {
    }
};

WEAK TraceBuffer *halide_trace_buffer = NULL;
WEAK int halide_trace_file = -1;  // -1 indicates uninitialized
WEAK int halide_trace_file_lock = 0;
WEAK bool halide_trace_file_initialized = false;
WEAK void *halide_trace_file_internally_opened = NULL;

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

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
        uint32_t trace_tag_bytes = e->trace_tag ? (strlen(e->trace_tag) + 1) : 1;
        uint32_t total_size_without_padding = header_bytes + value_bytes + coords_bytes + name_bytes + trace_tag_bytes;
        uint32_t total_size = (total_size_without_padding + 3) & ~3;

        // Claim some space to write to in the trace buffer
        halide_trace_packet_t *packet = halide_trace_buffer->acquire_packet(user_context, fd, total_size);

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
        memcpy((void *)packet->trace_tag(), e->trace_tag ? e->trace_tag : "", trace_tag_bytes);

        // Release it
        halide_trace_buffer->release_packet(packet);

        // We should also flush the trace buffer if we hit an event
        // that might be the end of the trace.
        if (e->event == halide_trace_end_pipeline) {
            halide_trace_buffer->flush(user_context, fd);
        }

    } else {
        uint8_t buffer[4096];
        Printer<StringStreamPrinter, sizeof(buffer)> ss(user_context, (char *)buffer);

        // Round up bits to 8, 16, 32, or 64
        int print_bits = 8;
        while (print_bits < e->type.bits) {
            print_bits <<= 1;
        }
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
                                     "End pipeline",
                                     "Tag"};

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
                        ss.write_float16_from_bits(((uint16_t *)(e->value))[i]);
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

        if (e->trace_tag && *e->trace_tag) {
            ss << " tag = \"" << e->trace_tag << "\"";
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

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK trace_fn halide_custom_trace = halide_default_trace;

}
}  // namespace Runtime
}  // namespace Halide

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
    ScopedSpinLock lock(&halide_trace_file_lock);
    if (halide_trace_file < 0) {
        const char *trace_file_name = getenv("HL_TRACE_FILE");
        if (trace_file_name) {
            void *file = fopen(trace_file_name, "ab");
            halide_assert(user_context, file && "Failed to open trace file\n");
            halide_set_trace_file(fileno(file));
            halide_trace_file_internally_opened = file;
            if (!halide_trace_buffer) {
                halide_trace_buffer = (TraceBuffer *)malloc(sizeof(TraceBuffer));
                halide_trace_buffer->init();
            }
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
        if (halide_trace_buffer) {
            free(halide_trace_buffer);
        }
        return ret;
    } else {
        return 0;
    }
}

namespace {
WEAK __attribute__((destructor)) void halide_trace_cleanup() {
    halide_shutdown_trace();
}
}  // namespace
}
