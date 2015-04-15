#include "runtime_internal.h"

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

extern "C" WIN32API void *GetProcAddress(void *, const char *);

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *find_symbol(const char *name) {
    return GetProcAddress(NULL, name);
}

}}}
