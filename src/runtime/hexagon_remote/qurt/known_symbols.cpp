#include "HalideRuntime.h"

#include <dlfcn.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "known_symbols.h"

void *lookup_symbol(const char *name, const known_symbol *map) {
    for (int i = 0; map[i].name; i++) {
        if (strncmp(name, map[i].name, strlen(map[i].name) + 1) == 0) {
            return map[i].addr;
        }
    }
    return NULL;
}

extern "C" {

// More symbols we need to support.
extern int qurt_hvx_lock();
extern int qurt_hvx_unlock();
extern int __hexagon_muldf3();
extern int __hexagon_divdf3();
extern int __hexagon_adddf3();
extern int __hexagon_divsf3();
extern int __hexagon_udivdi3();
extern int __hexagon_udivsi3();
extern int __hexagon_umodsi3();
extern int __hexagon_divsi3();
extern int __hexagon_modsi3();
extern int __hexagon_subdf3();
extern float sqrtf(float);
extern double sqrt(double);
extern float expf(float);
extern double exp(double);
extern float logf(float);
extern double log(double);
extern float powf(float, float);
extern double pow(double, double);
extern float sinf(float);
extern double sin(double);
extern float cosf(float);
extern double cos(double);
extern float tanf(float);
extern double tan(double);
extern float asinf(float);
extern double asin(double);
extern float acosf(float);
extern double acos(double);
extern float atanf(float);
extern double atan(double);
extern float atan2f(float, float);
extern double atan2(double, double);
extern float sinhf(float);
extern double sinh(double);
extern float coshf(float);
extern double cosh(double);
extern float tanhf(float);
extern double tanh(double);
extern float asinhf(float);
extern double asinh(double);
extern float acoshf(float);
extern double acosh(double);
extern float atanhf(float);
extern double atanh(double);
extern float nearbyintf(float);
extern double nearbyint(double);
extern float truncf(float);
extern double trunc(double);
extern float floorf(float);
extern double floor(double);
extern float ceilf(float);
extern double ceil(double);
extern ssize_t write(int, const void *, size_t);

}  // extern "C"

void *get_known_symbol(const char *name) {
    static known_symbol known_syms[] = {
        {"abort", (char *)(&abort)},
        {"atoi", (char *)(&atoi)},
        {"close", (char *)(&close)},
        {"exit", (char *)(&exit)},
        {"fclose", (char *)(&fclose)},
        {"fopen", (char *)(&fopen)},
        {"free", (char *)(&free)},
        {"fwrite", (char *)(&fwrite)},
        {"getenv", (char *)(&getenv)},
        {"malloc", (char *)(&malloc)},
        {"memcmp", (char *)(&memcmp)},
        {"memcpy", (char *)(&memcpy)},
        {"memmove", (char *)(&memmove)},
        {"memset", (char *)(&memset)},
        {"memalign", (char *)(*memalign)},
        {"strcmp", (char *)(&strcmp)},
        {"strchr", (char *)(char *(*)(char *, int))(&strchr)},
        {"strlen", (char *)(int (*)(const char *))(&strlen)},
        {"strstr", (char *)(char *(*)(char *, const char *))(&strstr)},
        {"strncmp", (char *)(&strncmp)},
        {"strncpy", (char *)(&strncpy)},
        {"write", (char *)(&write)},

        {"halide_error", (char *)(&halide_error)},
        {"halide_print", (char *)(&halide_print)},
        {"halide_profiler_get_state", (char *)(&halide_profiler_get_state)},
        {"qurt_hvx_lock", (char *)(&qurt_hvx_lock)},
        {"qurt_hvx_unlock", (char *)(&qurt_hvx_unlock)},

        {"__hexagon_divdf3", (char *)(&__hexagon_divdf3)},
        {"__hexagon_muldf3", (char *)(&__hexagon_muldf3)},
        {"__hexagon_adddf3", (char *)(&__hexagon_adddf3)},
        {"__hexagon_subdf3", (char *)(&__hexagon_subdf3)},
        {"__hexagon_divsf3", (char *)(&__hexagon_divsf3)},
        {"__hexagon_udivdi3", (char *)(&__hexagon_udivdi3)},
        {"__hexagon_udivsi3", (char *)(&__hexagon_udivsi3)},
        {"__hexagon_umodsi3", (char *)(&__hexagon_umodsi3)},
        {"__hexagon_divsi3", (char *)(&__hexagon_divsi3)},
        {"__hexagon_modsi3", (char *)(&__hexagon_modsi3)},
        {"__hexagon_sqrtf", (char *)(&sqrtf)},
        {"sqrtf", (char *)(&sqrtf)},
        {"sqrt", (char *)(static_cast<double (*)(double)>(&sqrt))},
        {"sinf", (char *)(&sinf)},
        {"expf", (char *)(&expf)},
        {"exp", (char *)(static_cast<double (*)(double)>(&exp))},
        {"logf", (char *)(&logf)},
        {"log", (char *)(static_cast<double (*)(double)>(&log))},
        {"powf", (char *)(&powf)},
        {"pow", (char *)(static_cast<double (*)(double, double)>(&pow))},
        {"sin", (char *)(static_cast<double (*)(double)>(&sin))},
        {"cosf", (char *)(&cosf)},
        {"cos", (char *)(static_cast<double (*)(double)>(&cos))},
        {"tanf", (char *)(&tanf)},
        {"tan", (char *)(static_cast<double (*)(double)>(&tan))},
        {"asinf", (char *)(&asinf)},
        {"asin", (char *)(static_cast<double (*)(double)>(&asin))},
        {"acosf", (char *)(&acosf)},
        {"acos", (char *)(static_cast<double (*)(double)>(&acos))},
        {"atanf", (char *)(&atanf)},
        {"atan", (char *)(static_cast<double (*)(double)>(&atan))},
        {"atan2f", (char *)(&atan2f)},
        {"atan2", (char *)(static_cast<double (*)(double, double)>(&atan2))},
        {"sinhf", (char *)(&sinhf)},
        {"sinh", (char *)(static_cast<double (*)(double)>(&sinh))},
        {"coshf", (char *)(&coshf)},
        {"cosh", (char *)(static_cast<double (*)(double)>(&cosh))},
        {"tanhf", (char *)(&tanhf)},
        {"tanh", (char *)(static_cast<double (*)(double)>(&tanh))},
        {"asinhf", (char *)(&asinhf)},
        {"asinh", (char *)(static_cast<double (*)(double)>(&asinh))},
        {"acoshf", (char *)(&acoshf)},
        {"acosh", (char *)(static_cast<double (*)(double)>(&acosh))},
        {"atanhf", (char *)(&atanhf)},
        {"atanh", (char *)(static_cast<double (*)(double)>(&atanh))},
        {"nearbyintf", (char *)(&nearbyintf)},
        {"nearbyint", (char *)(static_cast<double (*)(double)>(&nearbyint))},
        {"truncf", (char *)(&truncf)},
        {"trunc", (char *)(static_cast<double (*)(double)>(&trunc))},
        {"floorf", (char *)(&floorf)},
        {"floor", (char *)(static_cast<double (*)(double)>(&floor))},
        {"ceilf", (char *)(&ceilf)},
        {"ceil", (char *)(static_cast<double (*)(double)>(&ceil))},
        {NULL, NULL}  // Null terminator.
    };

    return lookup_symbol(name, known_syms);
}
