#ifndef HALIDE_RUNTIME_PRINTER_H
#define HALIDE_RUNTIME_PRINTER_H
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

namespace {
template<PrinterType printer_type, uint64_t buffer_length = default_printer_buffer_length>
class Printer {
    char *buf, *dst, *end;
    void *user_context;
    bool own_mem;

public:
    explicit Printer(void *ctx, char *mem = nullptr)
        : user_context(ctx), own_mem(mem == nullptr) {
        if (mem != nullptr) {
            buf = mem;
        } else {
            buf = (char *)malloc(buffer_length);
        }

        dst = buf;
        if (dst) {
            end = buf + (buffer_length - 1);
            *end = 0;
        } else {
            // Pointers equal ensures no writes to buffer via formatting code
            end = dst;
        }
    }

    // Not movable, not copyable
    Printer(const Printer &copy) = delete;
    Printer &operator=(const Printer &) = delete;
    Printer(Printer &&) = delete;
    Printer &operator=(Printer &&) = delete;

    Printer &operator<<(const char *arg) {
        dst = halide_string_to_string(dst, end, arg);
        return *this;
    }

    Printer &operator<<(int64_t arg) {
        dst = halide_int64_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(int32_t arg) {
        dst = halide_int64_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(uint64_t arg) {
        dst = halide_uint64_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(uint32_t arg) {
        dst = halide_uint64_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(double arg) {
        dst = halide_double_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(float arg) {
        dst = halide_double_to_string(dst, end, arg, 0);
        return *this;
    }

    Printer &operator<<(const void *arg) {
        dst = halide_pointer_to_string(dst, end, arg);
        return *this;
    }

    Printer &write_float16_from_bits(const uint16_t arg) {
        double value = halide_float16_bits_to_double(arg);
        dst = halide_double_to_string(dst, end, value, 1);
        return *this;
    }

    Printer &operator<<(const halide_type_t &t) {
        dst = halide_type_to_string(dst, end, &t);
        return *this;
    }

    Printer &operator<<(const halide_buffer_t &buf) {
        dst = halide_buffer_to_string(dst, end, &buf);
        return *this;
    }

    // Use it like a stringstream.
    const char *str() {
        if (buf) {
            if (printer_type == StringStreamPrinterType) {
                msan_annotate_is_initialized();
            }
            return buf;
        } else {
            return allocation_error();
        }
    }

    // Clear it. Useful for reusing a stringstream.
    void clear() {
        dst = buf;
        if (dst) {
            dst[0] = 0;
        }
    }

    // Returns the number of characters in the buffer
    uint64_t size() const {
        return (uint64_t)(dst - buf);
    }

    uint64_t capacity() const {
        return buffer_length;
    }

    // Delete the last N characters
    void erase(int n) {
        if (dst) {
            dst -= n;
            if (dst < buf) {
                dst = buf;
            }
            dst[0] = 0;
        }
    }

    const char *allocation_error() {
        return "Printer buffer allocation failed.\n";
    }

    void msan_annotate_is_initialized() {
        (void)halide_msan_annotate_memory_is_initialized(user_context, buf, dst - buf + 1);
    }

    ~Printer() {
        if (!buf) {
            halide_error(user_context, allocation_error());
        } else {
            msan_annotate_is_initialized();
            if (printer_type == ErrorPrinterType) {
                halide_error(user_context, buf);
            } else if (printer_type == BasicPrinterType) {
                halide_print(user_context, buf);
            } else {
                // It's a stringstream. Do nothing.
            }
        }

        if (own_mem) {
            free(buf);
        }
    }
};

// A class that supports << with all the same types as Printer, but
// does nothing and should compile to a no-op.
class SinkPrinter {
public:
    explicit SinkPrinter(void *user_context) {
    }
};
template<typename T>
SinkPrinter operator<<(const SinkPrinter &s, T) {
    return s;
}

template<uint64_t buffer_length = default_printer_buffer_length>
using BasicPrinter = Printer<BasicPrinterType, buffer_length>;

template<uint64_t buffer_length = default_printer_buffer_length>
using ErrorPrinter = Printer<ErrorPrinterType, buffer_length>;

template<uint64_t buffer_length = default_printer_buffer_length>
using StringStreamPrinter = Printer<StringStreamPrinterType, buffer_length>;

using print = BasicPrinter<>;
using error = ErrorPrinter<>;
using stringstream = StringStreamPrinter<>;

#ifdef DEBUG_RUNTIME
using debug = BasicPrinter<>;
#else
using debug = SinkPrinter;
#endif
}  // namespace

// A Printer that automatically reserves stack space for the printer buffer, rather than malloc.
// Note that this requires an explicit buffer_length, and it (generally) should be <= 256.
template<PrinterType printer_type, uint64_t buffer_length>
class StackPrinter : public Printer<printer_type, buffer_length> {
    char scratch[buffer_length];

public:
    explicit StackPrinter(void *ctx)
        : Printer<printer_type, buffer_length>(ctx, scratch) {
        static_assert(buffer_length <= 256, "StackPrinter is meant only for small buffer sizes; you are probably making a mistake.");
    }
};

template<uint64_t buffer_length = default_printer_buffer_length>
using StackBasicPrinter = StackPrinter<BasicPrinterType, buffer_length>;

template<uint64_t buffer_length = default_printer_buffer_length>
using StackErrorPrinter = StackPrinter<ErrorPrinterType, buffer_length>;

template<uint64_t buffer_length = default_printer_buffer_length>
using StackStringStreamPrinter = StackPrinter<StringStreamPrinterType, buffer_length>;

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
#endif
