#include "mini_ion.h"
#include "mini_mman.h"
#include "android_ioctl.h"
#include "scoped_mutex_lock.h"
#include "printer.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Ion {

typedef int ion_user_handle_t;

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

// A ion fd defined in this module with weak linkage
WEAK int dev_ion_fd = -1;
WEAK halide_mutex thread_lock = { { 0 } };

// The default implementation of halide_ion_get_descriptor uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of get_descriptor must implement the following
// behavior:
// - halide_ion_get_descriptor should always store a valid file descriptor to
//   /dev/ion in fd, or return an error code.
extern "C" WEAK int halide_ion_get_descriptor(void *user_context, int *fd, bool create = true) {
    // TODO: Should we use a more "assertive" assert? these asserts do
    // not block execution on failure.
    halide_assert(user_context, fd != NULL);

    ScopedMutexLock lock(&thread_lock);

    // If the context has not been initialized, initialize it now.
    if (dev_ion_fd == -1 && create) {
        debug(user_context) << "    open /dev/ion -> ";
        dev_ion_fd = open("/dev/ion", O_RDONLY, 0);
        debug(user_context) << "        " << dev_ion_fd << "\n";
        if (dev_ion_fd == -1) {
            error(user_context) << "Failed to open /dev/ion.\n";
        }
    }

    *fd = dev_ion_fd;
    if (dev_ion_fd == -1) {
        return -1;
    } else {
        return 0;
    }
}

WEAK ion_user_handle_t ion_alloc(int ion_fd, size_t len, size_t align, unsigned int heap_id_mask, unsigned int flags) {
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

WEAK void ion_free(int ion_fd, ion_user_handle_t ion_handle) {
    if(ioctl(ion_fd, ION_IOC_FREE, &ion_handle) < 0) {
        // warn?
    }
}

WEAK int ion_map(int ion_fd, ion_user_handle_t ion_handle) {
    ion_fd_data map;
    map.handle = ion_handle;
    if (ioctl(ion_fd, ION_IOC_MAP, &map) < 0) {
        return -1;
    }
    return map.fd;
}

struct allocation_record {
    int dev_ion;
    ion_user_handle_t handle;
    int fd;
    void *mapped;
    size_t size;
};

// Allocate an ION buffer, and map it, returning the mapped pointer.
WEAK void *ion_alloc(void *user_context, size_t len, int heap_id, int *out_fd) {
    int dev_ion = -1;
    int result = halide_ion_get_descriptor(user_context, &dev_ion);
    if (result != 0) return NULL;

    const size_t align = 4096;
    const int flags = 1;  // cached

    // Align the allocation size.
    len = (len + align - 1) & ~(align - 1);

    // Allocate enough space to hold information about the allocation prior to the pointer we return.
    len += align;

    ion_user_handle_t ion_h = ion_alloc(dev_ion, len, align, 1 << heap_id, flags);

    int buf_fd = ion_map(dev_ion, ion_h);

    debug(user_context) << "    mmap map_size=" << (uint64_t)len << " Read Write Shared fd=" << buf_fd << " -> ";
    void *mem = mmap(NULL, len, MapProtection::Read | MapProtection::Write, MapFlags::Shared, buf_fd, 0);
    if (mem == MAP_FAILED) {
        ion_free(dev_ion, ion_h);
        debug(user_context) << "        MAP_FAILED\n";
        error(user_context) << "mmap failed\n";
        return NULL;
    } else {
        debug(user_context) << "        " << mem << "\n";
    }

    // Store a record of the ION allocation data before the pointer we return.
    allocation_record rec = { dev_ion, ion_h, buf_fd, mem, len };
    mem = reinterpret_cast<char *>(mem) + align;
    halide_assert(user_context, sizeof(allocation_record) <= align);
    memcpy(reinterpret_cast<allocation_record *>(mem) - 1, &rec, sizeof(rec));

    if (out_fd) {
        *out_fd = buf_fd;
    }

    return mem;
}

// Free a previously allocated ION buffer.
WEAK void ion_free(void *user_context, void *ion) {
    if (!ion) return;
    allocation_record rec = *(reinterpret_cast<allocation_record *>(ion) - 1);
    munmap(rec.mapped, rec.size);

    ion_free(rec.dev_ion, rec.handle);
}

}}}}  // namespace Halide::Runtime::Internal::Ion
