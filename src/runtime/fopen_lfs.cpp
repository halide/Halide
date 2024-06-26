#include "runtime_internal.h"

extern "C" void *fopen64(const char *, const char *);

extern "C" WEAK_INLINE void *halide_fopen(const char *filename, const char *type) {
    return fopen64(filename, type);
}
