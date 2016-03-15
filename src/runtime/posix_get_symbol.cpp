#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

void *dlopen(const char *, int);
void *dlsym(void *, const char *);
char *dlerror();

#define RTLD_LAZY 0x1

WEAK void *halide_get_symbol(const char *name) {
    return dlsym(NULL, name);
}

WEAK void *halide_load_library(const char *name) {
    void *lib = dlopen(name, RTLD_LAZY);
    if (!lib) {
        debug(NULL) << "dlerror: " << dlerror() << "\n";
    }
    return lib;
}

WEAK void *halide_get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

}
