#include "runtime_internal.h"

// mingw is missing some math library functions. This runtime module provides them.
extern "C" float sinf(float);
extern "C" float cosf(float);
extern "C" float logf(float);
extern "C" float sqrtf(float);
extern "C" double sin(double);
extern "C" double cos(double);
extern "C" double log(double);
extern "C" double sqrt(double);

// These functions may stay in the module even if not apparently
// used. LLVM emits calls to them during CodeGen to optimize pairs of
// sin/cos calls. Put them in a special section so that they get
// coalesced (similar to weak linkage).
extern "C" __attribute__((section(".gnu.linkonce.sincos"))) void sincosf(float x, float *s, float *c) {
    if (s) {
        *s = sinf(x);
    }
    if (c) {
        *c = cosf(x);
    }
}

extern "C" __attribute__((section(".gnu.linkonce.sincos"))) void sincos(double x, double *s, double *c) {
    if (s) {
        *s = sin(x);
    }
    if (c) {
        *c = cos(x);
    }
}

// These are also missing from MinGW's math library. They're OK to be
// weak here, and converted to linkonce in
// LLVM_Runtime_Linker.cpp. They'll be stripped if not used.
extern "C" WEAK __attribute__((always_inline)) float asinhf(float x) {
    return logf(x + sqrtf(1 + x*x));
}

extern "C" WEAK __attribute__((always_inline)) double asinh(double x) {
    return log(x + sqrt(1 + x*x));
}


extern "C" WEAK __attribute__((always_inline)) float acoshf(float x) {
    return logf(x + sqrtf(x*x - 1));
}

extern "C" WEAK __attribute__((always_inline)) double acosh(double x) {
    return log(x + sqrt(x*x - 1));
}

extern "C" WEAK __attribute__((always_inline)) float atanhf(float x) {
    return 0.5f * logf((1 + x) / (1 - x));
}

extern "C" WEAK __attribute__((always_inline)) double atanh(double x) {
    return 0.5 * log((1 + x) / (1 - x));
}
