#include "runtime_internal.h"
#include "device_interface.h"
#include "HalideRuntimeHexagonHost.h"
#include "printer.h"
#include "mini_ion.h"
#include "mini_mman.h"
#include "cuda_opencl_shared.h"
#include "scoped_mutex_lock.h"

#define INLINE inline __attribute__((always_inline))

#define ALIGNMENT 4096

#define O_TRUNC 00001000
#define O_CREAT 00000100

namespace Halide { namespace Runtime { namespace Internal { namespace Hexagon {

extern WEAK halide_device_interface hexagon_device_interface;

// A ion fd defined in this module with weak linkage
WEAK int ion_fd = -1;
WEAK halide_mutex thread_lock = { { 0 } };

// Define dynamic version of hexagon_remote/halide_hexagon_remote.h
typedef struct _remote_buffer__seq_octet _remote_buffer__seq_octet;
typedef _remote_buffer__seq_octet remote_buffer;
struct _remote_buffer__seq_octet {
   unsigned char* data;
   int dataLen;
};
typedef int (*remote_initialize_kernels_fn)(const unsigned char*, int, halide_hexagon_handle_t*);
typedef halide_hexagon_handle_t (*remote_get_symbol_fn)(halide_hexagon_handle_t, const char*, int);
typedef int (*remote_run_fn)(halide_hexagon_handle_t, int,
                             const remote_buffer*, int, const remote_buffer*, int,
                             remote_buffer*, int);
typedef int (*remote_release_kernels_fn)(halide_hexagon_handle_t, int);

WEAK remote_initialize_kernels_fn remote_initialize_kernels = NULL;
WEAK remote_get_symbol_fn remote_get_symbol = NULL;
WEAK remote_run_fn remote_run = NULL;
WEAK remote_release_kernels_fn remote_release_kernels = NULL;

template <typename T>
T get_symbol(void *user_context, void *host_lib, const char* name) {
    debug(user_context) << "    halide_get_library_symbol('" << name << "') -> \n";
    T sym = (T)halide_get_library_symbol(host_lib, name);
    debug(user_context) << "        " << (void *)sym << "\n";
    if (!sym) {
        error(user_context) << "Hexagon runtime symbol '" << name << "' not found.\n";
    }
    return sym;
}

// Load the hexagon remote runtime.
WEAK int init_hexagon_runtime(void *user_context) {
    if (remote_initialize_kernels && remote_run && remote_release_kernels) {
        // Already loaded.
        return 0;
    }

    debug(user_context) << "Hexagon: init_hexagon_runtime (user_context: " << user_context << ")\n";

    // Load the library.
    const char *host_lib_name = "libhalide_hexagon_host.so";
    debug(user_context) << "    halide_load_library('" << host_lib_name << "') -> \n";
    void *host_lib = halide_load_library(host_lib_name);
    debug(user_context) << "        " << host_lib << "\n";
    if (!host_lib) {
        error(user_context) << host_lib_name << " not found.\n";
        return -1;
    }

    // Get the symbols we need from the library.
    remote_initialize_kernels = get_symbol<remote_initialize_kernels_fn>(user_context, host_lib, "halide_hexagon_remote_initialize_kernels");
    if (!remote_initialize_kernels) return -1;
    remote_get_symbol = get_symbol<remote_get_symbol_fn>(user_context, host_lib, "halide_hexagon_remote_get_symbol");
    if (!remote_get_symbol) return -1;
    remote_run = get_symbol<remote_run_fn>(user_context, host_lib, "halide_hexagon_remote_run");
    if (!remote_run) return -1;
    remote_release_kernels = get_symbol<remote_release_kernels_fn>(user_context, host_lib, "halide_hexagon_remote_release_kernels");
    if (!remote_release_kernels) return -1;

    return 0;
}

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    halide_hexagon_handle_t module;
    size_t size;
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

    ScopedMutexLock lock(&thread_lock);

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

WEAK int halide_hexagon_initialize_kernels(void *user_context, void **state_ptr,
                                           const uint8_t *code, size_t code_size) {
    init_hexagon_runtime(user_context);

    debug(user_context) << "Hexagon: halide_hexagon_initialize_kernels (user_context: " << user_context
                        << ", state_ptr: " << state_ptr
                        << ", *state_ptr: " << *state_ptr
                        << ", code: " << code
                        << ", code_size: " << (int)code_size << ")\n";
    halide_assert(user_context, state_ptr != NULL);

    // Create the state object if necessary. This only happens once,
    // regardless of how many times halide_hexagon_initialize_kernels
    // or halide_hexagon_device_release is called.
    // halide_hexagon_device_release traverses this list and releases
    // the module objects, but it does not modify the list nodes
    // created/inserted here.
    ScopedMutexLock lock(&thread_lock);

    module_state **state = (module_state**)state_ptr;
    if (!(*state)) {
        debug(user_context) << "    allocating module state -> \n";
        *state = (module_state*)malloc(sizeof(module_state));
        debug(user_context) << "        " << *state << "\n";
        (*state)->module = 0;
        (*state)->size = 0;
        (*state)->next = state_list;
        state_list = *state;
    }

    int result = 0;

    // Create the module itself if necessary.
    if (!(*state)->module) {
        const char *filename = "/data/local/tmp/halide_kernels.so";
        debug(user_context) << "    open " << filename << " -> ";
        int so_fd = open(filename, O_RDWR | O_TRUNC | O_CREAT, 0);
        debug(user_context) << "        " << so_fd << "\n";
        if (so_fd == -1) {
            error(user_context) << "open failed.\n";
            return -1;
        }
        write(so_fd, code, code_size);
        close(so_fd);

        debug(user_context) << "    halide_remote_initialize_kernels -> ";
        halide_hexagon_handle_t module = 0;
        result = remote_initialize_kernels((uint8_t*)filename, strlen(filename) + 1, &module);
        if (result == 0) {
            debug(user_context) << "        " << module << "\n";
            (*state)->module = module;
            (*state)->size = code_size;
        } else {
            debug(user_context) << "        " << result << "\n";
        }
    } else {
        debug(user_context) << "    re-using existing module " << (*state)->module << "\n";
    }

    return result != 0 ? -1 : 0;
}

namespace {

// Use ION_IOC_MAP to get a file descriptor for a buffer.
WEAK int get_buffer_descriptor(void *user_context, ion_user_handle_t handle) {
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

// Prepare an array of remote_buffer arguments, mapping buffers if
// necessary. Only arguments with flags&flag_mask == flag_value are
// added to the mapped_args array. Returns the number of arguments
// mapped, or a negative number on error.
WEAK int map_arguments(void *user_context, size_t arg_count,
                       size_t arg_sizes[], void *args[], int arg_flags[], int flag_mask, int flag_value,
                       remote_buffer *mapped_args) {
    int mapped_count = 0;
    for (size_t i = 0; i < arg_count; i++) {
        if ((arg_flags[i] & flag_mask) != flag_value) continue;
        if (arg_flags[i] != 0) {
            // This is a buffer, map it and put the mapped buffer into
            // the result.
            halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));

            int prot = 0;
            if (arg_flags[i] & 1) prot |= MapProtocol::Read;
            if (arg_flags[i] & 2) prot |= MapProtocol::Write;

            uint64_t device_handle = halide_get_device_handle(*(uint64_t *)args[i]);
            ion_user_handle_t handle = reinterpret<ion_user_handle_t>(static_cast<uint32_t>(device_handle >> 32));
            int buf_fd = get_buffer_descriptor(user_context, handle);
            size_t map_size = static_cast<uint32_t>(device_handle & 0xFFFFFFFF);

            map_size = (map_size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
            debug(user_context) << "    mmap map_size=" << (uint64_t)map_size
                                << " prot=" << prot
                                << " Shared "
                                << "fd=" << buf_fd << " -> ";
            mapped_args[mapped_count].data = (uint8_t*)mmap(NULL, map_size, prot, MapFlags::Shared, buf_fd, 0);
            if (mapped_args[mapped_count].data == MAP_FAILED) {
                debug(user_context) << "        MAP_FAILED\n";
                error(user_context) << "mmap failed\n";
                return -1;
            } else {
                debug(user_context) << "        " << mapped_args[mapped_count].data << "\n";
            }
            mapped_args[mapped_count].dataLen = map_size;
        } else {
            // This is a scalar, just put the pointer/size in the result.
            mapped_args[mapped_count].data = (uint8_t*)args[i];
            mapped_args[mapped_count].dataLen = arg_sizes[i];
        }
        mapped_count++;
    }
    return mapped_count;
}

// Unmap an array of arguments previously mapped with map_arguments.
WEAK void unmap_arguments(void *user_context, size_t count, int arg_flags[],
                          int flags_mask, int flag_value, remote_buffer *mapped_args) {
    for (size_t i = 0; i < count; i++) {
        if ((arg_flags[i] & flags_mask) == flag_value) {
            halide_assert(user_context, arg_flags[i] != 0);
            // This is a buffer, we need to unmap it.
            debug(user_context) << "    munmap " << mapped_args->data << " " << mapped_args->dataLen << " -> \n";
            int result = munmap(mapped_args->data, mapped_args->dataLen);
            debug(user_context) << "        " << result << "\n";
            mapped_args++;
        }
    }
}

}  // namespace

WEAK int halide_hexagon_run(void *user_context,
                            void *state_ptr,
                            const char *name,
                            halide_hexagon_handle_t* function,
                            size_t arg_sizes[],
                            void *args[],
                            int arg_flags[]) {
    halide_assert(user_context, state_ptr != NULL);
    halide_assert(user_context, function != NULL);
    init_hexagon_runtime(user_context);

    halide_hexagon_handle_t module = state_ptr ? ((module_state *)state_ptr)->module : 0;
    debug(user_context) << "Hexagon: halide_hexagon_run ("
                        << "user_context: " << user_context << ", "
                        << "state_ptr: " << state_ptr << " (" << module << ") "
                        << "name: " << name
                        << "function: " << function << ")\n";

    int result = -1;

    // If we haven't gotten the symbol for this function, do so now.
    if (*function == 0) {
        debug(user_context) << "    halide_hexagon_remote_get_symbol " << name << " -> \n";
        *function = remote_get_symbol(module, name, strlen(name) + 1);
        debug(user_context) << "        " << *function << "\n";
        if (*function == 0) {
            error(user_context) << "Failed to find function " << name << " in module.\n";
            return -1;
        }
    }

    // Allocate some remote_buffer objects on the stack.
    int arg_count = 0;
    while(arg_sizes[arg_count] > 0) arg_count++;
    remote_buffer *mapped_buffers =
        (remote_buffer *)__builtin_alloca(arg_count * sizeof(remote_buffer));

    // Map the arguments.
    // First grab the input buffers.
    remote_buffer *input_buffers = mapped_buffers;
    int input_buffer_count = map_arguments(user_context, arg_count, arg_sizes, args, arg_flags, 0x3, 0x1,
                                           input_buffers);
    if (input_buffer_count < 0) return input_buffer_count;

    // Then the input scalars.
    remote_buffer *input_scalars = input_buffers + input_buffer_count;
    int input_scalar_count = map_arguments(user_context, arg_count, arg_sizes, args, arg_flags, 0x3, 0x0,
                                           input_scalars);
    if (input_scalar_count < 0) return input_scalar_count;

    // And the output buffers.
    remote_buffer *output_buffers = input_scalars + input_scalar_count;
    int output_buffer_count = map_arguments(user_context, arg_count, arg_sizes, args, arg_flags, 0x2, 0x2,
                                            output_buffers);
    if (output_buffer_count < 0) return output_buffer_count;

    // Call the pipeline on the device side.
    debug(user_context) << "    halide_hexagon_remote_run -> \n";
    result = remote_run(module, *function,
                        input_buffers, input_buffer_count,
                        input_scalars, input_scalar_count,
                        output_buffers, output_buffer_count);
    debug(user_context) << "        " << result << "\n";

    // Unmap the arguments. Scalars don't need to be unmapped.
    unmap_arguments(user_context, arg_count, arg_flags, 0x3, 0x1, input_buffers);
    unmap_arguments(user_context, arg_count, arg_flags, 0x2, 0x2, output_buffers);

    return result != 0 ? -1 : 0;
}

WEAK int halide_hexagon_device_release(void *user_context) {
    debug(user_context)
        << "Ion: halide_hexagon_device_release (user_context: " <<  user_context << ")\n";

    int fd = -1;
    int err = halide_hexagon_get_descriptor(user_context, &fd, false);
    if (err != 0) return err;

    ScopedMutexLock lock(&thread_lock);

    if (fd != -1) {
        // Release all of the remote side modules.
        module_state *state = state_list;
        while (state) {
            if (state->module) {
                debug(user_context) << "    halide_hexagon_remote_release_kernels " << state
                                    << " (" << state->module << ") -> ";
                int result = remote_release_kernels(state->module, state->size);
                debug(user_context) << "        " << result << "\n";
                state->module = 0;
                state->size = 0;
            }
            state = state->next;
        }

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

    err = halide_hexagon_wrap_device_handle(user_context, buf, handle, size);
    if (err != 0) {
        ion_free(fd, handle);
        return err;
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

    ion_user_handle_t handle = halide_hexagon_detach_device_handle(user_context, buf);

    debug(user_context) << "    ion_free handle=" << handle << "\n";
    ion_free(fd, handle);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

namespace {

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
    ion_user_handle_t handle = halide_hexagon_get_device_handle(user_context, buf);
    int buf_fd = get_buffer_descriptor(user_context, handle);
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
    ion_user_handle_t handle = halide_hexagon_get_device_handle(user_context, buf);
    int buf_fd = get_buffer_descriptor(user_context, handle);
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

WEAK int halide_hexagon_wrap_device_handle(void *user_context, struct buffer_t *buf,
                                                ion_user_handle_t handle, size_t size) {
    halide_assert(user_context, buf->dev == 0);
    if (buf->dev != 0) {
        return -2;
    }

    size_t max_size = 0xFFFFFFFF;
    halide_assert(user_context, size <= max_size);
    if (size > max_size) {
        return halide_error_buffer_allocation_too_large(user_context, "", size, max_size);
    }

    // We store the size in the lower 32 bits, and the handle in the upper 32 bits.
    uint64_t device_handle = static_cast<uint64_t>(reinterpret<uint32_t>(handle)) << 32 | size;

    buf->dev = halide_new_device_wrapper(device_handle, &hexagon_device_interface);
    if (buf->dev == 0) {
        return -1;
    }
    return 0;
}

WEAK ion_user_handle_t halide_hexagon_detach_device_handle(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return -1;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &hexagon_device_interface);
    uint64_t device_handle = halide_get_device_handle(buf->dev);

    halide_delete_device_wrapper(buf->dev);
    buf->dev = 0;
    return reinterpret<ion_user_handle_t>(device_handle >> 32);
}

WEAK ion_user_handle_t halide_hexagon_get_device_handle(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return -1;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &hexagon_device_interface);
    uint64_t device_handle = halide_get_device_handle(buf->dev);
    return reinterpret<ion_user_handle_t>(static_cast<uint32_t>(device_handle >> 32));
}

WEAK size_t halide_hexagon_get_device_size(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return 0;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &hexagon_device_interface);
    uint64_t device_handle = halide_get_device_handle(buf->dev);
    return static_cast<size_t>(device_handle & 0xFFFFFFFF);
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
