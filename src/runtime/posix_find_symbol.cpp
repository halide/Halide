#include "runtime_internal.h"

extern "C" void *dlsym(void *, const char *);

namespace Halide { namespace Runtime { namespace Internal {

void *find_symbol(const char *name) {
    return dlsym(NULL, name);
}

}}}
