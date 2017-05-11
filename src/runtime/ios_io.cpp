#include "HalideRuntime.h"
#include "objc_support.h"

extern "C" {

WEAK void halide_default_print(void *user_context, const char *str) {
    void *pool = create_autorelease_pool();
    ns_log_utf8_string(str);
    drain_autorelease_pool(pool);
}

}  // extern "C"
