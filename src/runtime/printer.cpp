#include "printer.h"
#include "HalideRuntime.h"

namespace Halide {
namespace Runtime {
namespace Internal {

static const char *const kAllocationError = "Printer buffer allocation failed.\n";

WEAK PrinterBase::PrinterBase(void *ctx, char *mem, uint64_t length)
    : user_context(ctx), length(length) {

    if (mem == nullptr) {
        mem = (char *)malloc(length);
        mem_to_free = mem;
    } else {
        mem_to_free = nullptr;
    }

    dst = buf = mem;
    if (dst) {
        end = buf + length - 1;
        *end = 0;
    } else {
        end = dst;
    }
}

WEAK PrinterBase::PrinterBase(void *ctx)
    : PrinterBase(ctx, nullptr, default_printer_buffer_length) {
}

WEAK PrinterBase &PrinterBase::operator<<(const char *arg) {
    dst = halide_string_to_string(dst, end, arg);
    return *this;
}

WEAK PrinterBase &PrinterBase::operator<<(int64_t arg) {
    dst = halide_int64_to_string(dst, end, arg, 1);
    return *this;
}

WEAK PrinterBase &PrinterBase::operator<<(int32_t arg) {
    dst = halide_int64_to_string(dst, end, arg, 1);
    return *this;
}

WEAK PrinterBase &PrinterBase::operator<<(uint64_t arg) {
    dst = halide_uint64_to_string(dst, end, arg, 1);
    return *this;
}

WEAK PrinterBase &PrinterBase::operator<<(uint32_t arg) {
    dst = halide_uint64_to_string(dst, end, arg, 1);
    return *this;
}

WEAK PrinterBase &PrinterBase::operator<<(double arg) {
    dst = halide_double_to_string(dst, end, arg, 1);
    return *this;
}

WEAK PrinterBase &PrinterBase::operator<<(float arg) {
    dst = halide_double_to_string(dst, end, arg, 0);
    return *this;
}

WEAK PrinterBase &PrinterBase::operator<<(const void *arg) {
    dst = halide_pointer_to_string(dst, end, arg);
    return *this;
}

WEAK PrinterBase &PrinterBase::write_float16_from_bits(const uint16_t arg) {
    double value = halide_float16_bits_to_double(arg);
    dst = halide_double_to_string(dst, end, value, 1);
    return *this;
}

WEAK PrinterBase &PrinterBase::operator<<(const halide_type_t &t) {
    dst = halide_type_to_string(dst, end, &t);
    return *this;
}

WEAK PrinterBase &PrinterBase::operator<<(const halide_buffer_t &buf) {
    dst = halide_buffer_to_string(dst, end, &buf);
    return *this;
}

// Use it like a stringstream.
WEAK const char *PrinterBase::str() {
    if (!buf) {
        return kAllocationError;
    }

    // This is really only needed for StringStreamPrinter, but is easier to do unconditionally
    halide_msan_annotate_memory_is_initialized(user_context, buf, dst - buf + 1);
    return buf;
}

// Clear it. Useful for reusing a stringstream.
WEAK void PrinterBase::clear() {
    dst = buf;
    if (dst) {
        dst[0] = 0;
    }
}

// Delete the last N characters
WEAK void PrinterBase::erase(int n) {
    if (dst) {
        dst -= n;
        if (dst < buf) {
            dst = buf;
        }
        dst[0] = 0;
    }
}

WEAK PrinterBase::~PrinterBase() {
    // free() is fine to call on a nullptr
    free(mem_to_free);
}

WEAK void PrinterBase::finish_error() {
    halide_error(user_context, str());
}

WEAK void PrinterBase::finish_print() {
    halide_print(user_context, str());
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
