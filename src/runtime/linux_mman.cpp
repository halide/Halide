#include "mini_mman.h"

extern "C" {

void *mmap(void *addr, size_t length, int prot, int flags, int fd, ssize_t offset);
int munmap(void *addr, size_t length);

#define MAP_FAILED ((void *)-1)

#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define PROT_EXEC 0x04

#define MAP_SHARED 0x01

}  // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *mmap(void *addr, size_t length, int prot, int flags, int fd, ssize_t offset) {
    return ::mmap(addr, length, prot, flags, fd, offset);
}

WEAK int munmap(void *addr, size_t length) {
    return ::munmap(addr, length);
}

}}}  // namespace Halide::Runtime::Internal
