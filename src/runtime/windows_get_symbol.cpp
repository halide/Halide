#include "HalideRuntime.h"

extern "C" {

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

WIN32API void *LoadLibraryA(const char *);
WIN32API void *GetProcAddress(void *, const char *);
WIN32API unsigned SetErrorMode(unsigned);
#define SEM_FAILCRITICALERRORS 0x0001
#define SEM_NOOPENFILEERRORBOX 0x8000

}  // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *halide_get_symbol_impl(const char *name) {
    return GetProcAddress(NULL, name);
}

WEAK void *halide_load_library_impl(const char *name) {
    // Suppress dialog windows during library open.
    unsigned old_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    void *lib = LoadLibraryA(name);
    SetErrorMode(old_mode);
    return lib;
}

WEAK void *halide_get_library_symbol_impl(void *lib, const char *name) {
    return GetProcAddress(lib, name);
}

WEAK halide_get_symbol_t custom_get_symbol = halide_get_symbol_impl;
WEAK halide_load_library_t custom_load_library = halide_load_library_impl;
WEAK halide_get_library_symbol_t custom_get_library_symbol = halide_get_library_symbol_impl;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK halide_get_symbol_t halide_set_custom_get_symbol(halide_get_symbol_t f) {
    halide_get_symbol_t result = custom_get_symbol;
    custom_get_symbol = f;
    return result;
}

WEAK halide_load_library_t halide_set_custom_load_library(halide_load_library_t f) {
    halide_load_library_t result = custom_load_library;
    custom_load_library = f;
    return result;
}

WEAK halide_get_library_symbol_t halide_set_custom_get_library_symbol(halide_get_library_symbol_t f) {
    halide_get_library_symbol_t result = custom_get_library_symbol;
    custom_get_library_symbol = f;
    return result;
}

WEAK void *halide_get_symbol(const char *name) {
    return custom_get_symbol(name);
}

WEAK void *halide_load_library(const char *name) {
    return custom_load_library(name);
}

WEAK void *halide_get_library_symbol(void *lib, const char *name) {
    return custom_get_library_symbol(lib, name);
}

}  // extern "C"
