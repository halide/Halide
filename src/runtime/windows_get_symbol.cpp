#include "HalideRuntime.h"

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

extern "C" {

WIN32API void *LoadLibraryA(const char *);
WIN32API void *GetProcAddress(void *, const char *);

WEAK void *halide_get_symbol(const char *name) {
    return GetProcAddress(NULL, name);
}

WEAK void *halide_load_library(const char *name) {
    return LoadLibraryA(name);
}

WEAK void *halide_get_library_symbol(void *lib, const char *name) {
    return GetProcAddress(lib, name);
}

}
