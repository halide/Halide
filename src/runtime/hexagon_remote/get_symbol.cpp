#include <HalideRuntime.h>

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <memory.h>

extern "C" {

// More symbols we need to support from halide_get_symbol.
extern int qurt_hvx_lock;
extern int qurt_hvx_unlock;
extern int __hexagon_muldf3;
extern int __hexagon_divdf3;
extern int __hexagon_adddf3;
extern int __hexagon_divsf3;
extern int __hexagon_udivdi3;
extern int __hexagon_udivsi3;
extern int __hexagon_umodsi3;
extern int __hexagon_divsi3;
extern int __hexagon_modsi3;
extern int __hexagon_subdf3;
extern int sqrtf;
extern int sqrt;
extern int expf;
extern int exp;
extern int logf;
extern int log;
extern int powf;
extern int pow;
extern int sinf;
extern int sin;
extern int cosf;
extern int cos;
extern int tanf;
extern int tan;
extern int asinf;
extern int asin;
extern int acosf;
extern int acos;
extern int atanf;
extern int atan;
extern int atan2f;
extern int atan2;
extern int sinhf;
extern int sinh;
extern int coshf;
extern int cosh;
extern int tanhf;
extern int tanh;
extern int asinhf;
extern int asinh;
extern int acoshf;
extern int acosh;
extern int atanhf;
extern int atanh;
extern int nearbyintf;
extern int nearbyint;
extern int truncf;
extern int trunc;
extern int floorf;
extern int floor;
extern int ceilf;
extern int ceil;
extern int write;

// These might not always be available.
__attribute__((weak)) extern int mmap;
__attribute__((weak)) extern int mprotect;
__attribute__((weak)) extern int munmap;

void *halide_get_symbol(const char *name) {
    // On the simulator, dlsym does not work at all, it appears to just
    // be a stub.
    // On device, dlsym has some very unpredictable behavior that makes
    // it randomly unable to find symbols. Rather than fight this
    // problem, we're just going to explicitly enumerate all of the
    // symbols we want.
    struct known_sym {
        const char *name;
        char *addr;
    };
    static known_sym known_syms[] = {
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
        {"mmap", (char *)(&mmap)},
        {"mprotect", (char *)(&mprotect)},
        {"munmap", (char *)(&munmap)},
        {"strcmp", (char *)(&strcmp)},
        {"strchr", (char *)(char *(*)(char *, int))(&strchr)},
        {"strlen", (char *)(int (*)(const char *))(&strlen)},
        {"strstr", (char *)(char *(*)(char *, const char *))(&strstr)},
        {"strncmp", (char *)(&strncmp)},
        {"strncpy", (char *)(&strncpy)},
        {"write", (char *)(&write)},

        {"halide_do_par_for", (char *)(&halide_do_par_for)},
        {"halide_do_task", (char *)(&halide_do_task)},
        {"halide_error", (char *)(&halide_error)},
        {"halide_free", (char *)(&halide_free)},
        {"halide_malloc", (char *)(&halide_malloc)},
        {"halide_mutex_lock", (char *)(&halide_mutex_lock)},
        {"halide_mutex_unlock", (char *)(&halide_mutex_unlock)},
        {"halide_mutex_destroy", (char *)(&halide_mutex_destroy)},
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
        {"sqrt", (char *)(&sqrt)},
        {"sinf", (char *)(&sinf)},
        {"expf", (char *)(&expf)},
        {"exp", (char *)(&exp)},
        {"logf", (char *)(&logf)},
        {"log", (char *)(&log)},
        {"powf", (char *)(&powf)},
        {"pow", (char *)(&pow)},
        {"sin", (char *)(&sin)},
        {"cosf", (char *)(&cosf)},
        {"cos", (char *)(&cos)},
        {"tanf", (char *)(&tanf)},
        {"tan", (char *)(&tan)},
        {"asinf", (char *)(&asinf)},
        {"asin", (char *)(&asin)},
        {"acosf", (char *)(&acosf)},
        {"acos", (char *)(&acos)},
        {"atanf", (char *)(&atanf)},
        {"atan", (char *)(&atan)},
        {"atan2f", (char *)(&atan2f)},
        {"atan2", (char *)(&atan2)},
        {"sinhf", (char *)(&sinhf)},
        {"sinh", (char *)(&sinh)},
        {"coshf", (char *)(&coshf)},
        {"cosh", (char *)(&cosh)},
        {"tanhf", (char *)(&tanhf)},
        {"tanh", (char *)(&tanh)},
        {"asinhf", (char *)(&asinhf)},
        {"asinh", (char *)(&asinh)},
        {"acoshf", (char *)(&acoshf)},
        {"acosh", (char *)(&acosh)},
        {"atanhf", (char *)(&atanhf)},
        {"atanh", (char *)(&atanh)},
        {"nearbyintf", (char *)(&nearbyintf)},
        {"nearbyint", (char *)(&nearbyint)},
        {"truncf", (char *)(&truncf)},
        {"trunc", (char *)(&trunc)},
        {"floorf", (char *)(&floorf)},
        {"floor", (char *)(&floor)},
        {"ceilf", (char *)(&ceilf)},
        {"ceil", (char *)(&ceil)},
        {NULL, NULL} // Null terminator.
    };

    for (int i = 0; known_syms[i].name; i++) {
        if (strncmp(name, known_syms[i].name, strlen(known_syms[i].name)+1) == 0) {
            return known_syms[i].addr;
        }
    }

    // We might as well try dlsym if we can't find the symbol we're
    // looking for.  We need to try both RTLD_SELF and
    // RTLD_DEFAULT. Sometimes, RTLD_SELF finds a symbol when
    // RTLD_DEFAULT does not. This is surprising, I *think* RLTD_SELF
    // should search a subset of the symbols searched by
    // RTLD_DEFAULT...
    void *def = dlsym(RTLD_SELF, name);
    if (def) {
        return def;
    }
    return dlsym(RTLD_DEFAULT, name);
}

void *halide_load_library(const char *name) {
    return dlopen(name, RTLD_LAZY);
}

void *halide_get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

}  // extern "C"
