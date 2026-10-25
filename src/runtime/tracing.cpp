#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_atomics.h"
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
// that's a bad name.
class SharedExclusiveSpinLock {
    volatile uint32_t lock = 0;

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
    ALWAYS_INLINE void acquire_shared() {
        using namespace Halide::Runtime::Internal::Synchronization;

        while (true) {
            uint32_t expected = lock & shared_mask;
            uint32_t desired = expected + 1;
            if (atomic_cas_strong_sequentially_consistent(&lock, &expected, &desired)) {
                return;
            }
        }
    }

    ALWAYS_INLINE void release_shared() {
        using namespace Halide::Runtime::Internal::Synchronization;

        atomic_fetch_sub_sequentially_consistent(&lock, (uint32_t)1);
    }

    ALWAYS_INLINE void acquire_exclusive() {
        using namespace Halide::Runtime::Internal::Synchronization;

        while (true) {
            // If multiple threads are trying to acquire exclusive
            // ownership, we may need to rerequest exclusive waiting
            // while we spin, as it gets unset whenever a thread
            // acquires exclusive ownership.
            atomic_fetch_or_sequentially_consistent(&lock, exclusive_waiting_mask);
            uint32_t expected = exclusive_waiting_mask;
            uint32_t desired = exclusive_held_mask;
            if (atomic_cas_strong_sequentially_consistent(&lock, &expected, &desired)) {
                return;
            }
        }
    }

    ALWAYS_INLINE void release_exclusive() {
        using namespace Halide::Runtime::Internal::Synchronization;

        atomic_fetch_and_sequentially_consistent(&lock, ~exclusive_held_mask);
    }

    ALWAYS_INLINE void init() {
        using namespace Halide::Runtime::Internal::Synchronization;

        uint32_t value = 0;
        atomic_store_sequentially_consistent(&lock, &value);
    }

    SharedExclusiveSpinLock() = default;
};

const static int buffer_size = 1024 * 1024;

class TraceBuffer {
    SharedExclusiveSpinLock lock;
    uint32_t cursor = 0, overage = 0;
    uint8_t buf[buffer_size];

    // Attempt to atomically acquire space in the buffer to write a
    // packet. Returns nullptr if the buffer was full.
    ALWAYS_INLINE halide_trace_packet_t *try_acquire_packet(void *user_context, uint32_t size) {
        using namespace Halide::Runtime::Internal::Synchronization;

        lock.acquire_shared();
        uint32_t my_cursor = atomic_fetch_add_sequentially_consistent(&cursor, size);
        if (my_cursor + size > sizeof(buf)) {
            // Don't try to back it out: instead, just allow this request to fail
            // (along with all subsequent requests) and record the 'overage'
            // that was added and should be ignored; then, in the next flush,
            // remove the overage.
            atomic_fetch_add_sequentially_consistent(&overage, size);
            lock.release_shared();
            return nullptr;
        } else {
            return (halide_trace_packet_t *)(buf + my_cursor);
        }
    }

public:
    // Wait for all writers to finish with their packets, stall any
    // new writers, and flush the buffer to the fd. Returns false if
    // the write failed.
    ALWAYS_INLINE bool flush(void *user_context, int fd) {
        lock.acquire_exclusive();
        bool success = true;
        if (cursor) {
            cursor -= overage;
            success = (cursor == (uint32_t)write(fd, buf, cursor));
            cursor = 0;
            overage = 0;
        }
        lock.release_exclusive();
        return success;
    }

    // Acquire and return a packet's worth of space in the trace
    // buffer, flushing the trace buffer to the given fd to make space
    // if necessary. The region acquired is protected from other
    // threads writing or reading to it, so it must be released before
    // a flush can occur. Returns nullptr if a flush failed. size must
    // be at most buffer_size.
    ALWAYS_INLINE halide_trace_packet_t *acquire_packet(void *user_context, int fd, uint32_t size) {
        halide_trace_packet_t *packet = nullptr;
        while (!(packet = try_acquire_packet(user_context, size))) {
            // Couldn't acquire space to write a packet. Flush and try again.
            if (!flush(user_context, fd)) {
                return nullptr;
            }
        }
        return packet;
    }

    // Release a packet, allowing it to be written out with flush
    ALWAYS_INLINE void release_packet(halide_trace_packet_t *) {
        using namespace Halide::Runtime::Internal::Synchronization;

        // Need a memory barrier to guarantee all the writes are done.
        atomic_thread_fence_sequentially_consistent();
        lock.release_shared();
    }

    ALWAYS_INLINE void init() {
        cursor = 0;
        overage = 0;
        lock.init();
    }

    TraceBuffer() = default;
};

WEAK TraceBuffer *halide_trace_buffer = nullptr;
WEAK int halide_trace_file = -1;  // -1 indicates uninitialized
WEAK ScopedSpinLock::AtomicFlag halide_trace_file_lock = 0;
WEAK void *halide_trace_file_internally_opened = nullptr;

// Flushes any buffered trace data to the current trace file, closes it if it
// was opened via HL_TRACE_FILE, and switches to fd. Returns zero on success.
// The caller must hold halide_trace_file_lock.
WEAK int set_trace_file_already_locked(void *user_context, int fd) {
    int result = 0;
    if (halide_trace_buffer && halide_trace_file > 0 &&
        !halide_trace_buffer->flush(user_context, halide_trace_file)) {
        result = -1;
    }
    if (halide_trace_file_internally_opened) {
        if (fclose(halide_trace_file_internally_opened) != 0) {
            result = -1;
        }
        halide_trace_file_internally_opened = nullptr;
    }
    if (fd > 0 && !halide_trace_buffer) {
        halide_trace_buffer = (TraceBuffer *)malloc(sizeof(TraceBuffer));
        halide_trace_buffer->init();
    } else if (fd <= 0 && halide_trace_buffer) {
        free(halide_trace_buffer);
        halide_trace_buffer = nullptr;
    }
    halide_trace_file = fd;
    return result;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

WEAK int32_t halide_default_trace(void *user_context, const halide_trace_event_t *e) {
    using namespace Halide::Runtime::Internal::Synchronization;

    static int32_t ids = 1;

    // Negative return values are error codes, so ids wrap within the
    // non-negative range.
    int32_t my_id = atomic_fetch_add_sequentially_consistent(&ids, 1) & 0x7fffffff;

    // If we're dumping to a file, use a binary format
    int fd = halide_get_trace_file(user_context);
    if (fd < 0) {
        // Some error condition
        return fd;
    } else if (fd > 0) {
        // Compute the total packet size
        uint32_t value_bytes = (e->event == halide_trace_load || e->event == halide_trace_store) ?
                                   (uint32_t)(e->lanes * e->type.bytes()) :
                                   0;
        uint32_t header_bytes = (uint32_t)sizeof(halide_trace_packet_t);
        uint32_t coords_bytes = e->dimensions * (uint32_t)sizeof(int32_t);
        uint32_t name_bytes = strlen(e->func) + 1;
        uint32_t trace_tag_bytes = e->trace_tag ? (strlen(e->trace_tag) + 1) : 1;
        uint32_t total_size_without_padding = header_bytes + value_bytes + coords_bytes + name_bytes + trace_tag_bytes;
        uint32_t total_size = (total_size_without_padding + 3) & ~3;
        if (total_size > buffer_size) {
            return halide_error_trace_failed(user_context, "Trace packet larger than the trace buffer");
        }

        // Claim some space to write to in the trace buffer
        halide_trace_packet_t *packet = halide_trace_buffer->acquire_packet(user_context, fd, total_size);
        if (!packet) {
            return halide_error_trace_failed(user_context, "Could not write to trace file");
        }

        // Write a packet into it. Zero the last word first so that the
        // padding is deterministic, which helps compression.
        ((uint32_t *)packet)[total_size / 4 - 1] = 0;
        packet->size = total_size;
        packet->event = e->event;
        packet->parent_id = e->parent_id;
        packet->dimensions = e->dimensions;
        if (e->event == halide_trace_load || e->event == halide_trace_store) {
            packet->value_index = e->value_index;
            packet->type_code = (uint8_t)e->type.code;
            packet->type_bits = e->type.bits;
            packet->lanes = (uint16_t)e->lanes;
        } else {
            packet->id = my_id;
            packet->thread_id = (e->event == halide_trace_begin_parallel_task) ? e->thread_id : 0;
        }
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
        if (e->event == halide_trace_end_pipeline &&
            !halide_trace_buffer->flush(user_context, fd)) {
            return halide_error_trace_failed(user_context, "Could not write to trace file");
        }

    } else if (fd == 0) {
        // Trace to stdout
        StringStreamPrinter<4096> ss(user_context);

        // Round up bits to 8, 16, 32, or 64
        int print_bits = 8;
        while (print_bits < e->type.bits) {
            print_bits <<= 1;
        }
        if (print_bits > 64) {
            return halide_error_trace_failed(user_context, "Tracing a type with more than 64 bits");
        }

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
                                     "Tag",
                                     "Begin parallel task",
                                     "End parallel task",
                                     "Bounds required"};

        // Only print out the value on stores and loads.
        bool print_value = (e->event < 2);

        ss << event_types[e->event] << " " << e->func << "." << e->value_index << "(";
        if (e->lanes > 1) {
            ss << "<";
        }
        for (int i = 0; i < e->dimensions; i++) {
            if (i > 0) {
                if ((e->lanes > 1) && (i % e->lanes) == 0) {
                    ss << ">, <";
                } else {
                    ss << ", ";
                }
            }
            ss << e->coordinates[i];
        }
        if (e->lanes > 1) {
            ss << ">)";
        } else {
            ss << ")";
        }

        if (print_value) {
            if (e->lanes > 1) {
                ss << " = <";
            } else {
                ss << " = ";
            }
            for (int i = 0; i < e->lanes; i++) {
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
                    if (print_bits < 16) {
                        return halide_error_trace_failed(user_context, "Tracing does not handle floats with fewer than 16 bits");
                    }
                    if (print_bits == 32) {
                        ss << ((float *)(e->value))[i];
                    } else if (print_bits == 16) {
                        ss << PrinterBase::Float16Bits{((uint16_t *)(e->value))[i]};
                    } else {
                        ss << ((double *)(e->value))[i];
                    }
                } else if (e->type.code == 3) {
                    ss << ((void **)(e->value))[i];
                }
            }
            if (e->lanes > 1) {
                ss << ">";
            }
        }

        if (e->trace_tag && *e->trace_tag) {
            ss << " tag = \"" << e->trace_tag << "\"";
        }
        if (e->thread_id != 0) {
            ss << " thread_id = " << e->thread_id;
        }

        ss << "\n";

        {
            ScopedSpinLock lock(&halide_trace_file_lock);
            halide_print(user_context, ss.str());
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

WEAK void halide_set_trace_file(void *user_context, int fd) {
    ScopedSpinLock lock(&halide_trace_file_lock);
    (void)set_trace_file_already_locked(user_context, fd);
}

extern int errno;

WEAK int halide_get_trace_file(void *user_context) {
    ScopedSpinLock lock(&halide_trace_file_lock);
    if (halide_trace_file < -1) {
        // Error
        return halide_error_trace_failed(user_context, "Bad trace file");
    } else if (halide_trace_file == -1) {
        // Uninitialized
        const char *trace_file_name = getenv("HL_TRACE_FILE");
        if (trace_file_name) {
            void *file = halide_fopen(trace_file_name, "ab");
            if (!file) {
                return halide_error_trace_failed(user_context, "Failed to open trace file");
            }
            (void)set_trace_file_already_locked(user_context, fileno(file));
            halide_trace_file_internally_opened = file;
        } else {
            (void)set_trace_file_already_locked(user_context, 0);
        }
    }
    return halide_trace_file;
}

WEAK int32_t halide_trace(void *user_context, const halide_trace_event_t *e) {
    return (*halide_custom_trace)(user_context, e);
}

WEAK int halide_shutdown_trace() {
    ScopedSpinLock lock(&halide_trace_file_lock);
    if (set_trace_file_already_locked(nullptr, -1) != 0) {
        return halide_error_code_trace_failed;
    }
    return halide_error_code_success;
}

namespace {
WEAK __attribute__((destructor)) void halide_trace_cleanup() {
    (void)halide_shutdown_trace();  // ignore errors
}
}  // namespace
}
