#include "runtime_internal.h"

extern "C" void *dlopen(const char *, int);
extern "C" void *dlsym(void *, const char *);
extern "C" int dlclose(void *);

#define RTLD_LAZY 0x1
#define RTLD_LOCAL 0x4

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *get_symbol(const char *name) {
    return dlsym(NULL, name);
}

WEAK void *load_library(const char *name) {
    return dlopen(name, RTLD_LAZY | RTLD_LOCAL);
}

WEAK void *get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

WEAK void free_library(void *lib) {
    dlclose(lib);
}

}}}
