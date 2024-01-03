#include "printer.h"

namespace Halide {
namespace Runtime {
namespace Internal {

PrinterBase::PrinterBase(void *user_context, char *start, char *end)
    : dst(start), end(end), start(start), user_context(user_context) {
    if (!start) {
        // Pointers equal ensures no writes to buffer via formatting code
        end = dst;
    } else {
        // null-terminate ahead of time
        *end = 0;
    }
}

const char *PrinterBase::str() {
    (void)halide_msan_annotate_memory_is_initialized(user_context, start, dst - start + 1);
    return start;
}

uint64_t PrinterBase::size() const {
    return (uint64_t)(dst - start);
}

uint64_t PrinterBase::capacity() const {
    return (uint64_t)(end - start);
}

void PrinterBase::clear() {
    dst = start;
    if (dst) {
        dst[0] = 0;
    }
}

void PrinterBase::erase(int n) {
    if (dst) {
        dst -= n;
        if (dst < start) {
            dst = start;
        }
        dst[0] = 0;
    }
}

void PrinterBase::allocation_error() const {
    halide_error(user_context, "Printer buffer allocation failed.\n");
}

PrinterBase &PrinterBase::operator<<(const char *arg) {
    dst = halide_string_to_string(dst, end, arg);
    return *this;
}

PrinterBase &PrinterBase::operator<<(int64_t arg) {
    dst = halide_int64_to_string(dst, end, arg, 1);
    return *this;
}

PrinterBase &PrinterBase::operator<<(int32_t arg) {
    dst = halide_int64_to_string(dst, end, arg, 1);
    return *this;
}

PrinterBase &PrinterBase::operator<<(uint64_t arg) {
    dst = halide_uint64_to_string(dst, end, arg, 1);
    return *this;
}

PrinterBase &PrinterBase::operator<<(uint32_t arg) {
    dst = halide_uint64_to_string(dst, end, arg, 1);
    return *this;
}

PrinterBase &PrinterBase::operator<<(double arg) {
    dst = halide_double_to_string(dst, end, arg, 1);
    return *this;
}

PrinterBase &PrinterBase::operator<<(float arg) {
    dst = halide_double_to_string(dst, end, arg, 0);
    return *this;
}

PrinterBase &PrinterBase::operator<<(PrinterBase::Float16Bits arg) {
    double value = halide_float16_bits_to_double(arg.bits);
    dst = halide_double_to_string(dst, end, value, 1);
    return *this;
}

PrinterBase &PrinterBase::operator<<(const void *arg) {
    dst = halide_pointer_to_string(dst, end, arg);
    return *this;
}

PrinterBase &PrinterBase::operator<<(const halide_type_t &t) {
    dst = halide_type_to_string(dst, end, &t);
    return *this;
}

PrinterBase &PrinterBase::operator<<(const halide_buffer_t &buf) {
    dst = halide_buffer_to_string(dst, end, &buf);
    return *this;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
