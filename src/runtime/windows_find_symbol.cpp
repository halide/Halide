#include "runtime_internal.h"

extern "C" void *GetProcAddress(void *, const char *);

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *find_symbol(const char *name) {
    return GetProcAddress(NULL, name);
}

}}}
