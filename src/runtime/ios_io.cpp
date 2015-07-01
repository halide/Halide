#include "runtime_internal.h"
#include "objc_apple_foundation_stubs.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void halide_print_impl(void *user_context, const char *str) {
    void *pool = halide_ns_create_autorelease_pool();
    halide_ns_log_utf8_string(str);
    halide_ns_release_and_free_autorelease_pool(pool);
}

}}} // namespace Halide::Runtime::Internal
