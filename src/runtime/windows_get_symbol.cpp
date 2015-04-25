#include "runtime_internal.h"

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

extern "C" WIN32API void *LoadLibrary(const char *);
extern "C" WIN32API void *GetProcAddress(void *, const char *);
extern "C" WIN32API int FreeLibrary(void *);

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *get_symbol(const char *name) {
    return GetProcAddress(NULL, name);
}

WEAK void *load_library(const char *name) {
    return LoadLibrary(name);
}

WEAK void *get_library_symbol(void *lib, const char *name) {
    return GetProcAddress(lib, name);
}

WEAK void free_library(void *lib) {
    FreeLibrary(lib);
}

}}}
