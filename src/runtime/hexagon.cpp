#include "runtime_internal.h"
#include "device_interface.h"
#include "HalideRuntimeHexagon.h"
#include "printer.h"
#include "mini_ion.h"
#include "mini_mman.h"
#include "cuda_opencl_shared.h"

#define INLINE inline __attribute__((always_inline))

#define ALIGNMENT 4096

namespace Halide { namespace Runtime { namespace Internal { namespace Hexagon {

extern WEAK halide_device_interface hexagon_device_interface;

// A ion fd defined in this module with weak linkage
int WEAK ion_fd = -1;
volatile int WEAK thread_lock = 0;

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    void *module;
    module_state *next;
};
WEAK module_state *state_list = NULL;

}}}}  // namespace Halide::Runtime::Internal::Hexagon

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::Ion;
using namespace Halide::Runtime::Internal::Hexagon;

extern "C" {

// The default implementation of halide_hexagon_get_descriptor uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of get_descriptor must implement the following
// behavior:
// - halide_hexagon_get_descriptor should always store a valid file descriptor to
//   /dev/ion in fd, or return an error code.
WEAK int halide_hexagon_get_descriptor(void *user_context, int *fd, bool create = true) {
    // TODO: Should we use a more "assertive" assert? these asserts do
    // not block execution on failure.
    halide_assert(user_context, fd != NULL);

    // If the context has not been initialized, initialize it now.
    if (ion_fd == -1 && create) {
        debug(user_context) << "    open /dev/ion -> ";
        ion_fd = ion_open();
        debug(user_context) << "        " << ion_fd << "\n";
        if (ion_fd == -1) {
            error(user_context) << "Failed to open /dev/ion.\n";
        }
    }

    *fd = ion_fd;
    if (ion_fd == -1) {
        return -1;
    } else {
        return 0;
    }
}


WEAK int halide_hexagon_initialize_kernels(void *user_context, void **module, const uint8_t *code, size_t size) {
    debug(user_context) << "Hexagon: halide_hexagon_initialize_kernels (user_context: " << user_context
                        << ", module: " << module
                        << ", *module: " << *module
                        << ", code: " << code
                        << ", size: " << (int)size << ")\n";
    halide_assert(user_context, module != 0);

    if (*module != 0) {
        // Already initialized.
        return 0;
    }

    // Set dummy module for now.
    *module = (void *)1;

    return 0;
}

namespace {

WEAK size_t count_arguments(void *user_context, size_t arg_sizes[], void *args[], int arg_flags[]) {
    size_t count = 0;
    while (arg_sizes[count] != 0) {
        debug(user_context) << "    halide_hexagon_run " << (int)count
                            << " " << (int)arg_sizes[count]
                            << " [" << (*((void **)args[count])) << " ...] "
                            << arg_flags[count] << "\n";
        count++;
    }
    return count;
}

WEAK void translate_arguments(void *user_context, size_t count,
                              size_t arg_sizes[], void *args[], int arg_flags[],
                              void **translated_args, uint64_t *dev_handles) {
    for (size_t i = 0; i < count; i++) {
        if (arg_flags[i] != 0) {
            halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
            dev_handles[i] = halide_get_device_handle(*(uint64_t *)args[i]);
            translated_args[i] = &(dev_handles[i]);
            debug(user_context) << "    halide_hexagon_run translated arg" << (int)i
                                << " [" << (*((void **)translated_args[i])) << " ...]\n";
        } else {
            translated_args[i] = args[i];
        }
    }
}

}  // namespace

WEAK int halide_hexagon_run(void *user_context,
                            void *module,
                            size_t offset,
                            const char *name,
                            size_t input_arg_sizes[],
                            void *input_args[],
                            int input_arg_flags[],
                            size_t output_arg_sizes[],
                            void *output_args[],
                            int output_arg_flags[]) {
    debug(user_context) << "Hexagon: halide_hexagon_run ("
                        << "user_context: " << user_context << ", "
                        << "module: " << module
                        << "offset: " << (int)offset
                        << "name: " << name << ")\n";

    size_t input_arg_count = count_arguments(user_context, input_arg_sizes, input_args, input_arg_flags);
    size_t output_arg_count = count_arguments(user_context, output_arg_sizes, output_args, output_arg_flags);

    void** translated_input_args = (void **)__builtin_alloca(input_arg_count * sizeof(void *));
    uint64_t *input_dev_handles = (uint64_t *)__builtin_alloca(input_arg_count * sizeof(uint64_t));
    translate_arguments(user_context, input_arg_count, input_arg_sizes, input_args, input_arg_flags, translated_input_args, input_dev_handles);
    void** translated_output_args = (void **)__builtin_alloca(output_arg_count * sizeof(void *));
    uint64_t *output_dev_handles = (uint64_t *)__builtin_alloca(output_arg_count * sizeof(uint64_t));
    translate_arguments(user_context, output_arg_count, output_arg_sizes, output_args, output_arg_flags, translated_output_args, output_dev_handles);

    return 0;
}

WEAK int halide_hexagon_device_release(void *user_context) {
    debug(user_context)
        << "Ion: halide_hexagon_device_release (user_context: " <<  user_context << ")\n";

    int fd = -1;
    int err = halide_hexagon_get_descriptor(user_context, &fd, false);
    if (err != 0) return err;

    if (fd != -1) {
        // Release all of the remote side modules.
        while (__sync_lock_test_and_set(&thread_lock, 1)) { }
        module_state *state = state_list;
        while (state) {
            if (state->module) {
                debug(user_context) << "    hexagon_remote_release_kernels" << state->module << "\n";
                //hexagon_remote_release_kernels(state->module);
                state->module = 0;
            }
            module_state *to_free = state;
            state = state->next;
            free(to_free);
        }
        __sync_lock_release(&thread_lock);

        // Only destroy the context if we own it
        if (fd == ion_fd) {
            debug(user_context) << "    close " << ion_fd << "\n";
            ion_close(ion_fd);
            ion_fd = -1;
        }
    }

    return 0;
}

WEAK int halide_hexagon_device_malloc(void *user_context, buffer_t *buf) {
    debug(user_context)
        << "Ion: halide_hexagon_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    if (buf->dev) {
        // This buffer already has a device allocation
        return 0;
    }

    int fd = -1;
    int err = halide_hexagon_get_descriptor(user_context, &fd);
    if (err != 0) return err;

    // System heap... should we be using a different heap for Hexagon?
    unsigned int heap_id = 25;

    size_t size = buf_size(user_context, buf);
    size_t align = 4096;
    unsigned int heap_id_mask = 1 << heap_id;
    unsigned int flags = 0;

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

    debug(user_context)
        << "    ion_alloc len=" << (uint64_t)size
        << ", align=" << (uint64_t)align
        << ", heap_id_mask=" << heap_id_mask
        << ", flags=" << flags << " -> ";
    ion_user_handle_t handle = ion_alloc(fd, size, align, heap_id_mask, flags);
    if (handle == -1) {
        debug(user_context) << "        error\n";
        error(user_context) << "ion_alloc failed\n";
        return -1;
    } else {
        debug(user_context) << "        " << handle << "\n";
    }

    buf->dev = halide_new_device_wrapper((uint64_t)handle, &hexagon_device_interface);
    if (buf->dev == 0) {
        ion_free(fd, handle);
        error(user_context) << "Out of memory allocating device wrapper.\n";
        return -1;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_hexagon_device_free(void *user_context, buffer_t* buf) {
    debug(user_context)
        << "Ion: halide_hexagon_device_free (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    int fd = -1;
    int err = halide_hexagon_get_descriptor(user_context, &fd);
    if (err != 0) return err;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    ion_user_handle_t handle = halide_hexagon_get_device_handle(user_context, buf);

    debug(user_context) << "    ion_free handle=" << handle << "\n";
    ion_free(fd, handle);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

namespace {

// Use ION_IOC_MAP to get a file descriptor for a buffer.
WEAK int get_buffer_descriptor(void *user_context, int handle) {
    // Get ion file descriptor.
    int fd = -1;
    int err = halide_hexagon_get_descriptor(user_context, &fd);
    if (err != 0) return -1;

    // Get the descriptor associated with the ion buffer.
    int buf_fd = ion_map(fd, handle);
    debug(user_context) << "    ion_map handle=" << handle << " -> ";
    if (buf_fd == -1) {
        debug(user_context) << "        failed\n";
        error(user_context) << "ion_map failed\n";
        return -1;
    } else {
        debug(user_context) << "        " << buf_fd << "\n";
    }

    return buf_fd;
}

// Implement a device copy using memcpy.
WEAK void do_memcpy(void *user_context, device_copy c) {
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
                    void *dst = (void *)(c.dst + off);
                    uint64_t size = c.chunk_size;
                    debug(user_context) << "    memcpy "
                                        << "(" << x << ", " << y << ", " << z << ", " << w << "), "
                                        << src << " -> " << (void *)dst << ", " << size << " bytes\n";
                    memcpy(dst, src, size);
                }
            }
        }
    }
}

}  // namespace

WEAK int halide_hexagon_copy_to_device(void *user_context, buffer_t* buf) {
    debug(user_context)
        <<  "Ion: halide_hexagon_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->host && buf->dev);
    device_copy c = make_host_to_device_copy(buf);

    // Get the descriptor associated with the ion buffer.
    int buf_fd = get_buffer_descriptor(user_context, c.dst);
    if (buf_fd == -1) return -1;

    // Map the descriptor.
    size_t map_size = buf_size(user_context, buf);
    map_size = (map_size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    debug(user_context) << "    mmap map_size=" << (uint64_t)map_size << " Write Shared fd=" << buf_fd << " -> ";
    void *c_dst = mmap(NULL, map_size, MapProtocol::Write, MapFlags::Shared, buf_fd, 0);
    if (c_dst == MAP_FAILED) {
        debug(user_context) << "        MAP_FAILED\n";
        error(user_context) << "mmap failed\n";
        return -1;
    } else {
        debug(user_context) << "        " << c_dst << "\n";
    }
    c.dst = (uintptr_t)c_dst;

    do_memcpy(user_context, c);

    munmap(c_dst, map_size);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_hexagon_copy_to_host(void *user_context, buffer_t* buf) {
    debug(user_context)
        << "Ion: halide_hexagon_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->host && buf->dev);
    device_copy c = make_device_to_host_copy(buf);

    // Get the descriptor associated with the ion buffer.
    int buf_fd = get_buffer_descriptor(user_context, c.src);
    if (buf_fd == -1) return -1;

    // Map the descriptor.
    size_t map_size = buf_size(user_context, buf);
    map_size = (map_size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    debug(user_context) << "    mmap map_size=" << (uint64_t)map_size << " Read Shared fd=" << buf_fd << " -> ";
    void *c_src = mmap(NULL, map_size, MapProtocol::Read, MapFlags::Shared, buf_fd, 0);
    if (c_src == MAP_FAILED) {
        debug(user_context) << "        MAP_FAILED\n";
        error(user_context) << "mmap failed\n";
        return -1;
    } else {
        debug(user_context) << "        " << c_src << "\n";
    }
    c.src = (uintptr_t)c_src;

    do_memcpy(user_context, c);

    munmap(c_src, map_size);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_hexagon_device_sync(void *user_context, struct buffer_t *) {
    debug(user_context)
        << "Ion: halide_cuda_device_sync (user_context: " << user_context << ")\n";
    // Nothing to do.
    return 0;
}

WEAK int halide_hexagon_wrap_device_handle(void *user_context, struct buffer_t *buf, uintptr_t ion_user_handle) {
    halide_assert(user_context, buf->dev == 0);
    if (buf->dev != 0) {
        return -2;
    }
    buf->dev = halide_new_device_wrapper(ion_user_handle, &hexagon_device_interface);
    if (buf->dev == 0) {
        return -1;
    }
    return 0;
}

WEAK uintptr_t halide_hexagon_detach_device_handle(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return -1;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &hexagon_device_interface);
    int handle = (int)halide_get_device_handle(buf->dev);
    halide_delete_device_wrapper(buf->dev);
    buf->dev = 0;
    return handle;
}

WEAK uintptr_t halide_hexagon_get_device_handle(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return -1;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &hexagon_device_interface);
    int handle = (int)halide_get_device_handle(buf->dev);
    return handle;
}

WEAK const halide_device_interface *halide_hexagon_device_interface() {
    return &hexagon_device_interface;
}

namespace {
__attribute__((destructor))
WEAK void halide_hexagon_cleanup() {
    halide_hexagon_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace Hexagon {

WEAK halide_device_interface hexagon_device_interface = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_hexagon_device_malloc,
    halide_hexagon_device_free,
    halide_hexagon_device_sync,
    halide_hexagon_device_release,
    halide_hexagon_copy_to_host,
    halide_hexagon_copy_to_device,
};

}}}} // namespace Halide::Runtime::Internal::Hexagon
