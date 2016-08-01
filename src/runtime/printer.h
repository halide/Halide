#ifndef HALIDE_RUNTIME_PRINTER_H
#define HALIDE_RUNTIME_PRINTER_H
namespace Halide { namespace Runtime { namespace Internal {

enum PrinterType {BasicPrinter = 0,
                  ErrorPrinter = 1,
                  StringStreamPrinter = 2};

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
template<int type, uint64_t length = 1024>
class Printer {
public:
    char *buf, *dst, *end;
    void *user_context;
    bool own_mem;

    Printer(void *ctx, char *mem = NULL) : user_context(ctx), own_mem(mem == NULL) {
        buf = mem ? mem : (char *)halide_malloc(user_context, length);
        dst = buf;
        if (dst) {
            end = buf + (length-1);
            *end = 0;
        } else {
            // Pointers equal ensures no writes to buffer via formatting code
            end = dst;
        }
    }

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

    Printer & write_float16_from_bits(const uint16_t arg) {
        double value = halide_float16_bits_to_double(arg);
        dst = halide_double_to_string(dst, end, value, 1);
        return *this;
    }

    // Use it like a stringstream.
    const char *str() {
        if (buf) {
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

    ~Printer() {
        if (!buf) {
            halide_error(user_context, allocation_error());
        } else if (type == ErrorPrinter) {
            halide_error(user_context, buf);
        } else if (type == BasicPrinter) {
            halide_print(user_context, buf);
        } else {
            // It's a stringstream. Do nothing.
        }

        if (own_mem) {
            halide_free(user_context, buf);
        }
    }
};

// A class that supports << with all the same types as Printer, but
// does nothing and should compile to a no-op.
class SinkPrinter {
public:
    SinkPrinter(void *user_context) {}
};
template<typename T>
SinkPrinter operator<<(const SinkPrinter &s, T) {
    return s;
}

typedef Printer<BasicPrinter> print;
typedef Printer<ErrorPrinter> error;
typedef Printer<StringStreamPrinter> stringstream;

#ifdef DEBUG_RUNTIME
typedef Printer<BasicPrinter> debug;
#else
typedef SinkPrinter debug;
#endif
}


}}}
#endif
