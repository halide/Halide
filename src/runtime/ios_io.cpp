#include "runtime_internal.h"
#include "objc_apple_foundation_stubs.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void halide_print_impl(void *user_context, const char *str) {
    AutoreleasePool autorelease;

    ns_log_utf8_string(str);
}

}}} // namespace Halide::Runtime::Internal
