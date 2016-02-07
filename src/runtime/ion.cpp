#include "runtime_internal.h"
#include "device_interface.h"
#include "HalideRuntimeIon.h"
#include "printer.h"
#include "mini_ion.h"
#include "mmap.h"
#include "cuda_opencl_shared.h"

#define INLINE inline __attribute__((always_inline))

namespace Halide { namespace Runtime { namespace Internal { namespace Ion {

extern WEAK halide_device_interface ion_device_interface;

// A ion fd defined in this module with weak linkage
int WEAK ion_fd = -1;
volatile int WEAK thread_lock = 0;

}}}} // namespace Halide::Runtime::Internal::Ion

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::Ion;

extern "C" {

// The default implementation of halide_ion_get_descriptor uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of get_descriptor must implement the following
// behavior:
// - halide_ion_get_descriptor should always store a valid file descriptor to
//   /dev/ion in fd, or return an error code.
WEAK int halide_ion_get_descriptor(void *user_context, int *fd, bool create = true) {
    // TODO: Should we use a more "assertive" assert? these asserts do
    // not block execution on failure.
    halide_assert(user_context, fd != NULL);

    halide_assert(user_context, &thread_lock != NULL);
    while (__sync_lock_test_and_set(&thread_lock, 1)) { }

    // If the context has not been initialized, initialize it now.
    if (ion_fd == -1 && create) {
        ion_fd = open("/dev/ion", O_RDONLY, 0);
    }

    __sync_lock_release(&thread_lock);

    *fd = ion_fd;
    if (ion_fd == -1) {
        return -1;
    } else {
        return 0;
    }
}

WEAK int halide_ion_device_release(void *user_context) {
    debug(user_context)
        << "Ion: halide_ion_device_release (user_context: " <<  user_context << ")\n";

    int fd = -1;
    int err = halide_ion_get_descriptor(user_context, &fd, false);
    if (err != 0) return err;

    if (fd != -1) {
        // Only destroy the context if we own it
        if (fd == ion_fd) {
            debug(user_context) << "    close " << ion_fd << "\n";
            close(ion_fd);
            ion_fd = -1;
        }
    }

    return 0;
}

WEAK int halide_ion_device_malloc(void *user_context, buffer_t *buf) {
    debug(user_context)
        << "Ion: halide_ion_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    int fd = -1;
    int err = halide_ion_get_descriptor(user_context, &fd);
    if (err != 0) return err;

    size_t size = buf_size(user_context, buf);
    if (buf->dev) {
        // This buffer already has a device allocation
        return 0;
    }

    halide_assert(user_context, buf->stride[0] >= 0 && buf->stride[1] >= 0 &&
                                buf->stride[2] >= 0 && buf->stride[3] >= 0);

    debug(user_context) << "    allocating buffer of " << (uint64_t)size << " bytes, "
                        << "extents: "
                        << buf->extent[0] << "x"
                        << buf->extent[1] << "x"
                        << buf->extent[2] << "x"
                        << buf->extent[3] << " "
                        << "strides: "
                        << buf->stride[0] << "x"
                        << buf->stride[1] << "x"
                        << buf->stride[2] << "x"
                        << buf->stride[3] << " "
                        << "(" << buf->elem_size << " bytes per element)\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    ion_allocation_data data;
    data.len = size;
    data.align = 32;
//    data.heap_id_mask = heap_mask;
//    data.flags = flags;

    if (ioctl(fd, ION_IOC_ALLOC, &data) < 0)
        return -1;

    buf->dev = halide_new_device_wrapper((uint64_t)data.handle, &ion_device_interface);
    if (buf->dev == 0) {
        error(user_context) << "Ion: out of memory allocating device wrapper.\n";
        ioctl(fd, ION_IOC_FREE, &data.handle);
        return -1;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_ion_device_free(void *user_context, buffer_t* buf) {
    debug(user_context)
        << "Ion: halide_ion_device_free (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    int fd = -1;
    int err = halide_ion_get_descriptor(user_context, &fd);
    if (err != 0) return err;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    ion_user_handle_t handle = (ion_user_handle_t)halide_get_device_handle(buf->dev);
    if (ioctl(fd, ION_IOC_FREE, &handle) < 0)
        return -1;

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_ion_copy_to_device(void *user_context, buffer_t* buf) {
    debug(user_context)
        <<  "Ion: halide_ion_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    int fd = -1;
    int err = halide_ion_get_descriptor(user_context, &fd);
    if (err != 0) return err;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->host && buf->dev);

    device_copy c = make_host_to_device_copy(buf);

    ion_fd_data data;
    data.handle = (ion_user_handle_t)halide_get_device_handle(c.dst);
    int ret = ioctl(fd, ION_IOC_MAP, &data);
    if (ret < 0)
        return ret;
    if (data.fd == -1) {
        return -1;
    }
    size_t map_size = buf_size(user_context, buf);
    char *c_dst = (char *)mmap(NULL, map_size, PROT_WRITE, 0, data.fd, 0);
    if (c_dst == MAP_FAILED) {
        return -1;
    }

    // TODO: Is this 32-bit or 64-bit? Leaving signed for now
    // in case negative strides.
    for (int w = 0; w < (int)c.extent[3]; w++) {
        for (int z = 0; z < (int)c.extent[2]; z++) {
            for (int y = 0; y < (int)c.extent[1]; y++) {
                for (int x = 0; x < (int)c.extent[0]; x++) {
                    uint64_t off = (x * c.stride_bytes[0] +
                                    y * c.stride_bytes[1] +
                                    z * c.stride_bytes[2] +
                                    w * c.stride_bytes[3]);
                    void *src = (void *)(c.src + off);
                    void *dst = (void *)(c_dst + off);
                    uint64_t size = c.chunk_size;
                    debug(user_context) << "    memcpy "
                                        << "(" << x << ", " << y << ", " << z << ", " << w << "), "
                                        << src << " -> " << (void *)dst << ", " << size << " bytes\n";
                    memcpy(dst, src, size);
                }
            }
        }
    }

    munmap(c_dst, map_size);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_ion_copy_to_host(void *user_context, buffer_t* buf) {
    debug(user_context)
        << "Ion: halide_ion_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    int fd = -1;
    int err = halide_ion_get_descriptor(user_context, &fd);
    if (err != 0) return err;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->dev && buf->dev);

    device_copy c = make_device_to_host_copy(buf);

    ion_fd_data data;
    data.handle = (ion_user_handle_t)halide_get_device_handle(c.src);
    int ret = ioctl(fd, ION_IOC_MAP, &data);
    if (ret < 0)
        return ret;
    if (data.fd == -1) {
        return -1;
    }
    size_t map_size = buf_size(user_context, buf);
    char *c_src = (char *)mmap(NULL, map_size, PROT_READ, 0, data.fd, 0);
    if (c_src == MAP_FAILED) {
        return -1;
    }

    // TODO: Is this 32-bit or 64-bit? Leaving signed for now
    // in case negative strides.
    for (int w = 0; w < (int)c.extent[3]; w++) {
        for (int z = 0; z < (int)c.extent[2]; z++) {
            for (int y = 0; y < (int)c.extent[1]; y++) {
                for (int x = 0; x < (int)c.extent[0]; x++) {
                    uint64_t off = (x * c.stride_bytes[0] +
                                    y * c.stride_bytes[1] +
                                    z * c.stride_bytes[2] +
                                    w * c.stride_bytes[3]);
                    void *src = (void *)(c_src + off);
                    void *dst = (void *)(c.dst + off);
                    uint64_t size = c.chunk_size;

                    debug(user_context) << "    memcpy "
                                        << "(" << x << ", " << y << ", " << z << ", " << w << "), "
                                        << (void *)src << " -> " << dst << ", " << size << " bytes\n";

                    memcpy(dst, src, size);
                }
            }
        }
    }

    munmap(c_src, map_size);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_ion_device_sync(void *user_context, struct buffer_t *) {
    debug(user_context)
        << "Ion: halide_cuda_device_sync (user_context: " << user_context << ")\n";
    // Nothing to do.
    return 0;
}

WEAK int halide_ion_wrap_device_ptr(void *user_context, struct buffer_t *buf, uintptr_t device_ptr) {
    halide_assert(user_context, buf->dev == 0);
    if (buf->dev != 0) {
        return -2;
    }
    buf->dev = halide_new_device_wrapper(device_ptr, &ion_device_interface);
    if (buf->dev == 0) {
        return -1;
    }
    return 0;
}

WEAK uintptr_t halide_ion_detach_device_ptr(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return 0;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &ion_device_interface);
    uint64_t dev_ptr = halide_get_device_handle(buf->dev);
    halide_delete_device_wrapper(buf->dev);
    buf->dev = 0;
    return (uintptr_t)dev_ptr;
}

WEAK uintptr_t halide_ion_get_device_ptr(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return 0;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &ion_device_interface);
    uint64_t dev_ptr = halide_get_device_handle(buf->dev);
    return (uintptr_t)dev_ptr;
}

namespace {
__attribute__((destructor))
WEAK void halide_ion_cleanup() {
    halide_ion_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace Ion {

WEAK halide_device_interface ion_device_interface = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_ion_device_malloc,
    halide_ion_device_free,
    halide_ion_device_sync,
    halide_ion_device_release,
    halide_ion_copy_to_host,
    halide_ion_copy_to_device,
};

}}}} // namespace Halide::Runtime::Internal::Ion
