#ifndef HALIDE_RUNTIME_PRINTER_H
#define HALIDE_RUNTIME_PRINTER_H

#include "HalideRuntime.h"

// This is useful for debugging threading issues in the Halide runtime:
// prefix all `debug()` statements with the thread id that did the logging.
// Left here (but disabled) for future reference.
#ifndef HALIDE_RUNTIME_PRINTER_LOG_THREADID
#define HALIDE_RUNTIME_PRINTER_LOG_THREADID 0
#endif

#if HALIDE_RUNTIME_PRINTER_LOG_THREADID
extern "C" int pthread_threadid_np(long thread, uint64_t *thread_id);
#endif

namespace Halide {
namespace Runtime {
namespace Internal {

enum PrinterType { BasicPrinterType = 0,
                   ErrorPrinterType = 1,
                   StringStreamPrinterType = 2 };

constexpr uint64_t default_printer_buffer_length = 1024;

// A class for constructing debug messages from the runtime. Dumps
// items into a stack array, then prints them when the object leaves
// scope using halide_print. Think of it as a stringstream that prints
// when it dies. Use it like this:

// debug(user_context) << "A" << b << c << "\n";

// If you use it like this:

// debug d(user_context);
// d << "A";
// d << b;
// d << c << "\n";

// Then remember the print only happens when the debug object leaves
// scope, which may print at a confusing time.

class PrinterBase {
protected:
    char *dst;
    char *const end;
    char *const start;
    void *const user_context;

    NEVER_INLINE void allocation_error() const {
        halide_error(user_context, "Printer buffer allocation failed.\n");
    }

public:
    // This class will stream text into the range [start, start + size - 1].
    // It does *not* assume any ownership of the memory; it assumes
    // the memory will remain valid for its lifespan, and doesn't
    // attempt to free any allocations. It also doesn't do any sanity
    // checking of the pointers, so if you pass in a null or bogus value,
    // it will attempt to use it.
    NEVER_INLINE PrinterBase(void *user_context_, char *start_, uint64_t size_)
        : dst(start_),
          // (If start is null, set end = start to ensure no writes are done)
          end(start_ ? start_ + size_ - 1 : start_),
          start(start_),
          user_context(user_context_) {
        if (end > start) {
            // null-terminate the final byte to ensure string isn't $ENDLESS
            *end = 0;
        }
    }

    NEVER_INLINE const char *str() {
        (void)halide_msan_annotate_memory_is_initialized(user_context, start, dst - start + 1);
        return start;
    }

    uint64_t size() const {
        halide_debug_assert(user_context, dst >= start);
        return (uint64_t)(dst - start);
    }

    uint64_t capacity() const {
        halide_debug_assert(user_context, end >= start);
        return (uint64_t)(end - start);
    }

    NEVER_INLINE void clear() {
        dst = start;
        if (dst) {
            dst[0] = 0;
        }
    }

    NEVER_INLINE void erase(int n) {
        if (dst) {
            dst -= n;
            if (dst < start) {
                dst = start;
            }
            dst[0] = 0;
        }
    }

    struct Float16Bits {
        uint16_t bits;
    };

    // These are NEVER_INLINE because Clang will aggressively inline
    // all of them, but the code size of calling out-of-line here is slightly
    // smaller, and we ~always prefer smaller code size when using Printer
    // in the runtime (it's a modest but nonzero difference).
    NEVER_INLINE PrinterBase &operator<<(const char *arg) {
        dst = halide_string_to_string(dst, end, arg);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(int64_t arg) {
        dst = halide_int64_to_string(dst, end, arg, 1);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(int32_t arg) {
        dst = halide_int64_to_string(dst, end, arg, 1);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(uint64_t arg) {
        dst = halide_uint64_to_string(dst, end, arg, 1);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(uint32_t arg) {
        dst = halide_uint64_to_string(dst, end, arg, 1);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(double arg) {
        dst = halide_double_to_string(dst, end, arg, 1);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(float arg) {
        dst = halide_double_to_string(dst, end, arg, 0);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(Float16Bits arg) {
        double value = halide_float16_bits_to_double(arg.bits);
        dst = halide_double_to_string(dst, end, value, 1);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(const void *arg) {
        dst = halide_pointer_to_string(dst, end, arg);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(const halide_type_t &t) {
        dst = halide_type_to_string(dst, end, &t);
        return *this;
    }

    NEVER_INLINE PrinterBase &operator<<(const halide_buffer_t &buf) {
        dst = halide_buffer_to_string(dst, end, &buf);
        return *this;
    }

    template<typename... Args>
    void append(const Args &...args) {
        ((*this << args), ...);
    }

    // Not movable, not copyable
    PrinterBase(const PrinterBase &copy) = delete;
    PrinterBase &operator=(const PrinterBase &) = delete;
    PrinterBase(PrinterBase &&) = delete;
    PrinterBase &operator=(PrinterBase &&) = delete;
};

namespace {

template<PrinterType printer_type, uint64_t buffer_length = default_printer_buffer_length>
class HeapPrinter : public PrinterBase {
public:
    NEVER_INLINE explicit HeapPrinter(void *user_context)
        : PrinterBase(user_context, (char *)malloc(buffer_length), buffer_length) {
        if (!start) {
            allocation_error();
        }

#if HALIDE_RUNTIME_PRINTER_LOG_THREADID
        uint64_t tid;
        pthread_threadid_np(0, &tid);
        *this << "(TID:" << tid << ")";
#endif
    }

    NEVER_INLINE ~HeapPrinter() {
        if (printer_type == ErrorPrinterType) {
            halide_error(user_context, str());
        } else if (printer_type == BasicPrinterType) {
            halide_print(user_context, str());
        } else {
            // It's a stringstream. Do nothing.
        }

        free(start);
    }
};
// A class that supports << with all the same types as Printer, but
// does nothing and should compile to a no-op.
class SinkPrinter {
public:
    ALWAYS_INLINE explicit SinkPrinter(void *user_context) {
    }
};
template<typename T>
ALWAYS_INLINE SinkPrinter operator<<(const SinkPrinter &s, T) {
    return s;
}

template<uint64_t buffer_length = default_printer_buffer_length>
using BasicPrinter = HeapPrinter<BasicPrinterType, buffer_length>;

template<uint64_t buffer_length = default_printer_buffer_length>
using ErrorPrinter = HeapPrinter<ErrorPrinterType, buffer_length>;

template<uint64_t buffer_length = default_printer_buffer_length>
using StringStreamPrinter = HeapPrinter<StringStreamPrinterType, buffer_length>;

using print = BasicPrinter<>;
using error = ErrorPrinter<>;
using stringstream = StringStreamPrinter<>;

#ifdef DEBUG_RUNTIME
using debug = BasicPrinter<>;
#else
using debug = SinkPrinter;
#endif

// A Printer that automatically reserves stack space for the printer buffer, rather than malloc.
// Note that this requires an explicit buffer_length, and it (generally) should be <= 256.
template<PrinterType printer_type, uint64_t buffer_length>
class StackPrinter : public PrinterBase {
    char scratch[buffer_length];

public:
    explicit StackPrinter(void *user_context)
        : PrinterBase(user_context, scratch, buffer_length) {
        static_assert(buffer_length <= 256, "StackPrinter is meant only for small buffer sizes; you are probably making a mistake.");
    }
};

template<uint64_t buffer_length = default_printer_buffer_length>
using StackBasicPrinter = StackPrinter<BasicPrinterType, buffer_length>;

template<uint64_t buffer_length = default_printer_buffer_length>
using StackErrorPrinter = StackPrinter<ErrorPrinterType, buffer_length>;

template<uint64_t buffer_length = default_printer_buffer_length>
using StackStringStreamPrinter = StackPrinter<StringStreamPrinterType, buffer_length>;

}  // namespace

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
#endif
