#ifndef HALIDE_RUNTIME_PRINTER_H
#define HALIDE_RUNTIME_PRINTER_H

#include "runtime_internal.h"

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
    char *buf, *dst, *end, *mem_to_free;
    void *const user_context;
    uint64_t const length;

public:
    PrinterBase &operator<<(const char *arg);
    PrinterBase &operator<<(int64_t arg);
    PrinterBase &operator<<(int32_t arg);
    PrinterBase &operator<<(uint64_t arg);
    PrinterBase &operator<<(uint32_t arg);
    PrinterBase &operator<<(double arg);
    PrinterBase &operator<<(float arg);
    PrinterBase &operator<<(const void *arg);
    PrinterBase &write_float16_from_bits(uint16_t arg);
    PrinterBase &operator<<(const halide_type_t &t);
    PrinterBase &operator<<(const halide_buffer_t &buf);

    const char *str();
    void clear();
    // Delete the last N characters
    void erase(int n);

    // Returns the number of characters in the buffer
    inline uint64_t size() const {
        return (uint64_t)(dst - buf);
    }

    inline uint64_t capacity() const {
        return length;
    }

protected:
    PrinterBase(void *ctx, char *mem, uint64_t length);

    // This ctor assumes length = default_printer_buffer_length and mem == nullptr,
    // which is the case for almost all callers.
    PrinterBase(void *ctx);

    ~PrinterBase();

    // This exists just to avoid inlining a couple of calls when we
    // could inline just a single one; since almost all instances of this
    // tend to be in error handling, debugging, etc., we want to optimize for
    // code size.
    void finish_error();
    void finish_print();

public:
    // (clang-tidy wants deleted member functions to be public)
    // Not movable, not copyable
    PrinterBase(const PrinterBase &copy) = delete;
    PrinterBase &operator=(const PrinterBase &) = delete;
    PrinterBase(PrinterBase &&) = delete;
    PrinterBase &operator=(PrinterBase &&) = delete;
};

template<PrinterType printer_type, uint64_t buffer_length = default_printer_buffer_length>
class Printer : public PrinterBase {
public:
    // Use SFINAE to select the single-arg ctor in the oh-so-common case of default buffer_length.
    // We can't use std::enable_if here, so we'll roll our own:
    template<bool B, class T = void>
    struct halide_enable_if {};

    template<class T>
    struct halide_enable_if<true, T> { typedef T type; };

    template<uint64_t buffer_length_here = buffer_length,
             typename halide_enable_if<buffer_length_here == default_printer_buffer_length, int>::type = 0>
    inline Printer(void *ctx)
        : PrinterBase(ctx) {
    }

    template<uint64_t buffer_length_here = buffer_length,
             typename halide_enable_if<buffer_length_here != default_printer_buffer_length, int>::type = 0>
    inline Printer(void *ctx)
        : PrinterBase(ctx, nullptr, buffer_length) {
    }

    inline Printer(void *ctx, char *mem)
        : PrinterBase(ctx, mem, buffer_length) {
    }

    inline ~Printer() {
        if constexpr (printer_type == ErrorPrinterType) {
            finish_error();
        } else if constexpr (printer_type == BasicPrinterType) {
            finish_print();
        } else {
            static_assert(printer_type == StringStreamPrinterType);
            // It's a stringstream. Do nothing.
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

// A Printer that automatically reserves stack space for the printer buffer, rather than malloc.
// Note that this requires an explicit buffer_length, and it (generally) should be <= 256.
template<PrinterType printer_type, uint64_t buffer_length>
class StackPrinter : public PrinterBase {
    char scratch[buffer_length];

public:
    explicit StackPrinter(void *ctx)
        : PrinterBase(ctx, scratch, buffer_length) {
        static_assert(buffer_length <= 256, "StackPrinter is meant only for small buffer sizes; you are probably making a mistake.");
    }

    inline ~StackPrinter() {
        if constexpr (printer_type == ErrorPrinterType) {
            finish_error();
        } else if constexpr (printer_type == BasicPrinterType) {
            finish_print();
        } else {
            static_assert(printer_type == StringStreamPrinterType);
            // It's a stringstream. Do nothing.
        }
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
