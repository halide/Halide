#include "runtime_internal.h"

extern "C" {

void *dlopen(const char *, int);
void *dlsym(void *, const char *);

#define RTLD_DEFAULT ((void *)-2)

#define RTLD_LAZY 0x1
#define RTLD_LOCAL 0x4

WEAK void *halide_get_symbol(const char *name) {
    return dlsym(RTLD_DEFAULT, name);
}

WEAK void *halide_load_library(const char *name) {
    return dlopen(name, RTLD_LAZY | RTLD_LOCAL);
}

WEAK void *halide_get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

}
