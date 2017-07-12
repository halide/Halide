#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

void *dlopen(const char *, int);
void *dlsym(void *, const char *);
char *dlerror();

#define RTLD_LAZY 0x1

WEAK void *halide_default_get_symbol(const char *name) {
    return dlsym(NULL, name);
}

WEAK void *halide_default_load_library(const char *name) {
    void *lib = dlopen(name, RTLD_LAZY);
    if (!lib) {
        debug(NULL) << "dlerror: " << dlerror() << "\n";
    }
    return lib;
}

WEAK void *halide_default_get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

}  // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

WEAK halide_get_symbol_t custom_get_symbol = halide_default_get_symbol;
WEAK halide_load_library_t custom_load_library = halide_default_load_library;
WEAK halide_get_library_symbol_t custom_get_library_symbol = halide_default_get_library_symbol;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK halide_get_symbol_t halide_set_custom_get_symbol(halide_get_symbol_t f) {
    halide_get_symbol_t result = custom_get_symbol;
    custom_get_symbol = f;
    return result;
}

WEAK halide_load_library_t halide_set_custom_load_library(halide_load_library_t f) {
    halide_load_library_t result = custom_load_library;
    custom_load_library = f;
    return result;
}

WEAK halide_get_library_symbol_t halide_set_custom_get_library_symbol(halide_get_library_symbol_t f) {
    halide_get_library_symbol_t result = custom_get_library_symbol;
    custom_get_library_symbol = f;
    return result;
}

WEAK void *halide_get_symbol(const char *name) {
    return custom_get_symbol(name);
}

WEAK void *halide_load_library(const char *name) {
    return custom_load_library(name);
}

WEAK void *halide_get_library_symbol(void *lib, const char *name) {
    return custom_get_library_symbol(lib, name);
}

}  // extern "C"
