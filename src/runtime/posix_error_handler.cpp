#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_internal.h"

extern "C" {

extern void abort();

WEAK void halide_default_error(void *user_context, const char *msg) {
    // Can't use StackBasicPrinter here because it limits size to 256
    constexpr int buf_size = 4096;
    char buf[buf_size];
    PrinterBase dst(user_context, buf, buf_size);
    dst << "Error: " << msg;
    const char *d = dst.str();
    if (d && *d && d[strlen(d) - 1] != '\n') {
        dst << "\n";
    }
    halide_print(user_context, dst.str());
    abort();
}
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK halide_error_handler_t error_handler = halide_default_error;

}
}  // namespace Runtime
}  // namespace Halide

extern "C" {

WEAK void halide_error(void *user_context, const char *msg) {
    (*error_handler)(user_context, msg);
}

WEAK halide_error_handler_t halide_set_error_handler(halide_error_handler_t handler) {
    halide_error_handler_t result = error_handler;
    error_handler = handler;
    return result;
}
}
