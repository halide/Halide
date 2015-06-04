#include "runtime_internal.h"

extern "C" {

void *dlopen(const char *, int);
void *dlsym(void *, const char *);
int dlclose(void *);

#define RTLD_LAZY 0x1

WEAK void *halide_get_symbol(const char *name) {
    return dlsym(NULL, name);
}

WEAK void *halide_load_library(const char *name) {
    return dlopen(name, RTLD_LAZY);
}

WEAK void *halide_get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

WEAK void halide_free_library(void *lib) {
    dlclose(lib);
}

}
