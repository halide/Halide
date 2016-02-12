#include "mini_ion.h"

#include "runtime_internal.h"
#include "android_ioctl.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Ion {

// Ion data structures.
struct ion_allocation_data {
    size_t len;
    size_t align;
    unsigned int heap_id_mask;
    unsigned int flags;
    ion_user_handle_t handle;
};

struct ion_fd_data {
    ion_user_handle_t handle;
    int fd;
};

struct ion_handle_data {
    ion_user_handle_t handle;
};

#define ION_IOC_MAGIC  'I'
#define ION_IOC_ALLOC  _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE   _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_MAP    _IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data)

// Halide ion interface implementation.
int ion_open() {
    return open("/dev/ion", O_RDONLY, 0);
}

void ion_close(int ion_fd) {
    close(ion_fd);
}

ion_user_handle_t ion_alloc(int ion_fd, size_t len, size_t align, unsigned int heap_id_mask, unsigned int flags) {
    ion_allocation_data alloc;
    alloc.len = len;
    alloc.align = align;
    alloc.heap_id_mask = heap_id_mask;
    alloc.flags = flags;
    if (ioctl(ion_fd, ION_IOC_ALLOC, &alloc) < 0) {
        return -1;
    }
    return alloc.handle;
}

void ion_free(int ion_fd, ion_user_handle_t ion_handle) {
    if(ioctl(ion_fd, ION_IOC_FREE, &ion_handle) < 0) {
        // warn?
    }
}

int ion_map(int ion_fd, ion_user_handle_t ion_handle) {
    ion_fd_data map;
    map.handle = ion_handle;
    if (ioctl(ion_fd, ION_IOC_MAP, &map) < 0) {
        return -1;
    }
    return map.fd;
}

}}}}  // namespace Halide::Runtime::Internal::Ion
