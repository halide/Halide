#include "HalideRuntime.h"

extern "C" {

void *dlopen(const char *, int);
void *dlsym(void *, const char *);

#define RTLD_DEFAULT ((void *)-2)

#define RTLD_LAZY 0x1
#define RTLD_LOCAL 0x4

WEAK void *halide_default_get_symbol(const char *name) {
    return dlsym(RTLD_DEFAULT, name);
}

WEAK void *halide_default_load_library(const char *name) {
    return dlopen(name, RTLD_LAZY | RTLD_LOCAL);
}

WEAK void *halide_default_get_library_symbol(void *lib, const char *name) {
    // We want our semantics to be such that if lib is NULL, this call
    // is equivalent to halide_get_symbol.
    if (lib == NULL) {
        lib = RTLD_DEFAULT;
    }
    return dlsym(lib, name);
}

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK halide_get_symbol_t custom_get_symbol = halide_default_get_symbol;
WEAK halide_load_library_t custom_load_library = halide_default_load_library;
WEAK halide_get_library_symbol_t custom_get_library_symbol = halide_default_get_library_symbol;

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

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
