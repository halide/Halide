#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

extern int __android_log_print(int, const char *, const char *, ...);

WEAK void __halide_print(void *user_context, const char * str) {
    __android_log_print(7, "halide", "%s", str);
}

}
