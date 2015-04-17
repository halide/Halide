#include "runtime_internal.h"

extern "C" void *dlsym(void *, const char *);

#define RTLD_DEFAULT ((void *)-2)

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *get_symbol(const char *name) {
    return dlsym(RTLD_DEFAULT, name);
}

}}}
