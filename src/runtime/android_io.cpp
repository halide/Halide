#include "HalideRuntime.h"

extern "C" {

#define ANDROID_LOG_INFO 4

extern int __android_log_print(int, const char *, const char *, ...);

}

namespace Halide { namespace Runtime { namespace Internal {

WEAK void halide_print_impl(void *user_context, const char * str) {
    __android_log_print(ANDROID_LOG_INFO, "halide", "%s", str);
}

}}}
