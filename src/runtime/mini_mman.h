#ifndef HALIDE_RUNTIME_MMAN_H
#define HALIDE_RUNTIME_MMAN_H

// This is a dependency-less implementation of sys/mman.h.

#include "runtime_internal.h"

namespace Halide { namespace Runtime { namespace Internal {

// Invalid map result.
#define MAP_FAILED ((void *)-1)

namespace MapProtection {
enum {
    Read = 1 << 0,
    Write = 1 << 1,
    Exec = 1 << 2,
};
};

namespace MapFlags {
enum {
    Shared = 1 << 0,
};
};

void *mmap(void *addr, size_t length, int prot, int flags, int fd, ssize_t offset);
int munmap(void *addr, size_t length);

}}}  // namespace Halide::Runtime::Internal

#endif
