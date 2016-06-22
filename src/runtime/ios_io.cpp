#include "HalideRuntime.h"
#include "objc_support.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void halide_print_impl(void *user_context, const char *str) {
    void *pool = create_autorelease_pool();
    ns_log_utf8_string(str);
    drain_autorelease_pool(pool);
}

}}} // namespace Halide::Runtime::Internal
