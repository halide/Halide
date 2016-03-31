#include "HalideRuntime.h"

// mingw is missing some math library functions. This runtime module provides them.
extern "C" {
extern float sinf(float);
extern float cosf(float);
extern float logf(float);
extern float sqrtf(float);
extern double sin(double);
extern double cos(double);
extern double log(double);
extern double sqrt(double);

WEAK void sincosf(float x, float *s, float *c) {
    if (s) {
        *s = sinf(x);
    }
    if (c) {
        *c = cosf(x);
    }
}

WEAK void sincos(double x, double *s, double *c) {
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
WEAK float asinhf(float x) {
    return logf(x + sqrtf(1 + x*x));
}

WEAK double asinh(double x) {
    return log(x + sqrt(1 + x*x));
}

WEAK float acoshf(float x) {
    return logf(x + sqrtf(x*x - 1));
}

WEAK double acosh(double x) {
    return log(x + sqrt(x*x - 1));
}

WEAK float atanhf(float x) {
    return 0.5f * logf((1 + x) / (1 - x));
}

WEAK double atanh(double x) {
    return 0.5 * log((1 + x) / (1 - x));
}

}
