#include "runtime_internal.h"
#include "device_interface.h"
#include "HalideRuntimeHexagonHost.h"
#include "printer.h"
#include "mini_ion.h"
#include "mini_mman.h"
#include "cuda_opencl_shared.h"
#include "scoped_mutex_lock.h"

#define INLINE inline __attribute__((always_inline))

#define O_TRUNC 00001000
#define O_CREAT 00000100
#define O_EXCL  00000200

namespace Halide { namespace Runtime { namespace Internal { namespace Hexagon {

struct ion_device_handle {
    void *buffer;
    size_t size;
};

WEAK halide_mutex thread_lock = { { 0 } };

extern WEAK halide_device_interface hexagon_device_interface;

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
typedef void (*remote_register_buf_fn)(void *, int, int);

WEAK remote_initialize_kernels_fn remote_initialize_kernels = NULL;
WEAK remote_get_symbol_fn remote_get_symbol = NULL;
WEAK remote_run_fn remote_run = NULL;
WEAK remote_release_kernels_fn remote_release_kernels = NULL;
WEAK remote_register_buf_fn remote_register_buf = NULL;

template <typename T>
void get_symbol(void *user_context, void *host_lib, const char* name, T &sym) {
    debug(user_context) << "    halide_get_library_symbol('" << name << "') -> \n";
    sym = (T)halide_get_library_symbol(host_lib, name);
    debug(user_context) << "        " << (void *)sym << "\n";
    if (!sym) {
        error(user_context) << "Required Hexagon runtime symbol '" << name << "' not found.\n";
    }
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

    get_symbol(user_context, host_lib, "halide_hexagon_remote_initialize_kernels", remote_initialize_kernels);
    if (!remote_initialize_kernels) return -1;
    get_symbol(user_context, host_lib, "halide_hexagon_remote_get_symbol", remote_get_symbol);
    if (!remote_get_symbol) return -1;
    get_symbol(user_context, host_lib, "halide_hexagon_remote_run", remote_run);
    if (!remote_run) return -1;
    get_symbol(user_context, host_lib, "halide_hexagon_remote_release_kernels", remote_release_kernels);
    if (!remote_release_kernels) return -1;

    // There's an optional symbol in libadsprpc.so that enables
    // buffers to be zero copy that we *try* to load.
    debug(user_context) << "    halide_load_library('libadsprpc.so') -> \n";
    void *adsprpc_lib = halide_load_library("libadsprpc.so");
    debug(user_context) << "        " << adsprpc_lib << "\n";
    if (adsprpc_lib) {
        debug(user_context) << "    halide_get_library_symbol('remote_register_buf') -> \n";
        remote_register_buf = (remote_register_buf_fn)halide_get_library_symbol(adsprpc_lib, "remote_register_buf");
        debug(user_context) << "        " << (void *)remote_register_buf << "\n";
        // This symbol is optional, don't error if it isn't available.
    }

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

namespace {

// This function writes the given data to a shared object file, returning the filename.
// TODO: Try writing this in a way that doesn't actually touch the file system (named pipe?)
WEAK int write_shared_object(void *user_context, const uint8_t *data, size_t size,
                             char *filename, size_t filename_size) {
    int result = halide_create_temp_file(user_context, "halide_kernels_", ".so", filename, filename_size);
    if (result != 0) {
        error(user_context) << "Unable to create temporary shared object file\n";
        return result;
    }

    int so_fd = open(filename, O_RDWR | O_TRUNC, 0755);
    if (so_fd == -1) {
        error(user_context) << "Failed to open shared object file " << filename << "\n";
        return halide_error_code_internal_error;
    }

    ssize_t written = write(so_fd, data, size);
    close(so_fd);
    if (written < (ssize_t)size) {
        error(user_context) << "Failed to write shared object file " << filename << "\n";
        return halide_error_code_internal_error;
    }

    debug(user_context) << "    Wrote temporary shared object '" << filename << "'\n";
    return 0;
}

}  // namespace

WEAK int halide_hexagon_initialize_kernels(void *user_context, void **state_ptr,
                                           const uint8_t *code, size_t code_size) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) return result;

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

    // Create the module itself if necessary.
    if (!(*state)->module) {
        char filename[260];
        result = write_shared_object(user_context, code, code_size, filename, sizeof(filename));
        if (result != 0) {
            return result;
        }

        debug(user_context) << "    halide_remote_initialize_kernels -> ";
        halide_hexagon_handle_t module = 0;
        result = remote_initialize_kernels((uint8_t*)filename, strlen(filename) + 1, &module);

        // Unfortunately, dlopen on the Hexagon side doesn't keep the
        // shared object alive in the file system. To work around
        // this, we open the shared object, then remove the file. The
        // file will still exist until our process exits, at which
        // time the file handle will be closed, and then the file will
        // be removed from the file system.
        open(filename, O_RDONLY, 0);
        remove(filename);

        if (result == 0) {
            debug(user_context) << "        " << module << "\n";
            (*state)->module = module;
            (*state)->size = code_size;
        } else {
            debug(user_context) << "        " << result << "\n";
            error(user_context) << "Initialization of Hexagon kernels failed\n";
        }
    } else {
        debug(user_context) << "    re-using existing module " << (*state)->module << "\n";
    }

    return result != 0 ? -1 : 0;
}

namespace {

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
        remote_buffer &mapped_arg = mapped_args[mapped_count++];
        if (arg_flags[i] != 0) {
            // This is a buffer, map it and put the mapped buffer into
            // the result.
            halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));

            uint64_t device_handle = halide_get_device_handle(*(uint64_t *)args[i]);
            ion_device_handle *ion_handle = reinterpret<ion_device_handle *>(device_handle);
            mapped_arg.data = reinterpret_cast<uint8_t*>(ion_handle->buffer);
            mapped_arg.dataLen = ion_handle->size;
        } else {
            // This is a scalar, just put the pointer/size in the result.
            mapped_arg.data = (uint8_t*)args[i];
            mapped_arg.dataLen = arg_sizes[i];
        }
    }
    return mapped_count;
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
    int result = init_hexagon_runtime(user_context);
    if (result != 0) return result;

    halide_hexagon_handle_t module = state_ptr ? ((module_state *)state_ptr)->module : 0;
    debug(user_context) << "Hexagon: halide_hexagon_run ("
                        << "user_context: " << user_context << ", "
                        << "state_ptr: " << state_ptr << " (" << module << "), "
                        << "name: " << name << ", "
                        << "function: " << function << " (" << *function << "))\n";

    // If we haven't gotten the symbol for this function, do so now.
    if (*function == 0) {
        debug(user_context) << "    halide_hexagon_remote_get_symbol " << name << " -> ";
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
    // First grab the input buffers (bit 0 of flags is set).
    remote_buffer *input_buffers = mapped_buffers;
    int input_buffer_count = map_arguments(user_context, arg_count, arg_sizes, args, arg_flags, 0x3, 0x1,
                                           input_buffers);
    if (input_buffer_count < 0) return input_buffer_count;

    // Then the output buffers (bit 1 of flags is set).
    remote_buffer *output_buffers = input_buffers + input_buffer_count;
    int output_buffer_count = map_arguments(user_context, arg_count, arg_sizes, args, arg_flags, 0x2, 0x2,
                                            output_buffers);
    if (output_buffer_count < 0) return output_buffer_count;

    // And the input scalars (neither bits 0 or 1 of flags is set).
    remote_buffer *input_scalars = output_buffers + output_buffer_count;
    int input_scalar_count = map_arguments(user_context, arg_count, arg_sizes, args, arg_flags, 0x3, 0x0,
                                           input_scalars);
    if (input_scalar_count < 0) return input_scalar_count;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    // Call the pipeline on the device side.
    debug(user_context) << "    halide_hexagon_remote_run -> ";
    result = remote_run(module, *function,
                        input_buffers, input_buffer_count,
                        output_buffers, output_buffer_count,
                        input_scalars, input_scalar_count);
    debug(user_context) << "        " << result << "\n";
    if (result != 0) {
        error(user_context) << "Hexagon pipeline failed.\n";
        return result;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return result != 0 ? -1 : 0;
}

WEAK int halide_hexagon_device_release(void *user_context) {
    debug(user_context)
        << "Hexagon: halide_hexagon_device_release (user_context: " <<  user_context << ")\n";

    ScopedMutexLock lock(&thread_lock);

    // Release all of the remote side modules.
    module_state *state = state_list;
    while (state) {
        if (state->module) {
            debug(user_context) << "    halide_remote_release_kernels " << state
                                << " (" << state->module << ") -> ";
            int result = remote_release_kernels(state->module, state->size);
            debug(user_context) << "        " << result << "\n";
            state->module = 0;
            state->size = 0;
        }
        state = state->next;
    }

    return 0;
}

// When allocations for Hexagon are at least as large as this
// threshold, use an ION allocation (to get zero copy). If the
// allocation is smaller, use a standard allocation instead.  This is
// done because allocating an entire page for a small allocation is
// wasteful, and the copy is not significant.  Additionally, the
// FastRPC interface can probably do a better job with many small
// arguments than simply mapping the pages.
static const int min_ion_allocation_size = 4096;

WEAK int halide_hexagon_device_malloc(void *user_context, buffer_t *buf) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) return result;

    debug(user_context)
        << "Hexagon: halide_hexagon_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    if (buf->dev) {
        // This buffer already has a device allocation
        return 0;
    }

    // System heap... should we be using a different heap for Hexagon?
    unsigned int heap_id = 25;

    size_t size = buf_size(user_context, buf);

    // Hexagon code generation generates clamped ramp loads in a way
    // that requires up to an extra vector beyond the end of the
    // buffer to be legal to access.
    size += 128;

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

    void *ion;
    if (size >= min_ion_allocation_size) {
        debug(user_context) << "    ion_alloc len=" << (uint64_t)size << ", heap_id=" << heap_id << " -> ";
        int ion_fd;
        ion = ion_alloc(user_context, size, heap_id, &ion_fd);
        debug(user_context) << "        " << ion << " (fd=" << ion_fd << ")\n";
        if (!ion) {
            error(user_context) << "ion_alloc failed\n";
            return -1;
        }

        if (remote_register_buf) {
            // If we have this symbol, it means that we can register the
            // buffer to get zero copy behavior with the DSP.
            debug(user_context) << "    remote_register_buf buf=" << ion << " size=" << (uint64_t)size
                                << " fd=" << ion_fd << "\n";
            remote_register_buf(ion, size, ion_fd);
        }
    } else {
        debug(user_context) << "    halide_malloc size=" << (uint64_t)size << " -> ";
        ion = halide_malloc(user_context, size);
        debug(user_context) << "        " << ion << "\n";
        if (!ion) {
            error(user_context) << "halide_malloc failed\n";
            return -1;
        }
    }

    int err = halide_hexagon_wrap_device_handle(user_context, buf, ion, size);
    if (err != 0) {
        ion_free(user_context, ion);
        return err;
    }

    if (!buf->host) {
        // If the host pointer has also not been allocated yet, set it to
        // the ion buffer. This buffer will be zero copy.
        buf->host = (uint8_t *)ion;
        debug(user_context) << "    host <- " << buf->host << "\n";
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_hexagon_device_free(void *user_context, buffer_t* buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_device_free (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    uint64_t size = halide_hexagon_get_device_size(user_context, buf);
    void *ion = halide_hexagon_detach_device_handle(user_context, buf);
    if (size >= min_ion_allocation_size) {
        debug(user_context) << "    ion_free ion=" << ion << "\n";
        ion_free(user_context, ion);

        if (remote_register_buf) {
            // If we have this symbol, we registered the buffer, so now we
            // unregister it (by setting fd = -1).
            size_t size = buf_size(user_context, buf);
            debug(user_context) << "    remote_register_buf buf=" << ion << " size=" << (int)size << " fd=-1\n";
            remote_register_buf(ion, size, -1);
        }
    } else {
        debug(user_context) << "    halide_free ion=" << ion << "\n";
        halide_free(user_context, ion);
    }

    if (buf->host == ion) {
        // If we also set the host pointer, reset it.
        buf->host = NULL;
        debug(user_context) << "    host <- 0x0\n";
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

namespace {

// Implement a device copy using memcpy.
WEAK void device_memcpy(void *user_context, device_copy c) {
    if (c.src == c.dst) {
        // This is a zero copy buffer, copy is a no-op.
        return;
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
    int err = halide_hexagon_device_malloc(user_context, buf);
    if (err) {
        return err;
    }

    debug(user_context)
        <<  "Hexagon: halide_hexagon_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->host && buf->dev);
    device_copy c = make_host_to_device_copy(buf);

    // Get the descriptor associated with the ion buffer.
    c.dst = reinterpret<uintptr_t>(halide_hexagon_get_device_handle(user_context, buf));
    device_memcpy(user_context, c);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_hexagon_copy_to_host(void *user_context, buffer_t* buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->host && buf->dev);
    device_copy c = make_device_to_host_copy(buf);

    // Get the descriptor associated with the ion buffer.
    c.src = reinterpret<uintptr_t>(halide_hexagon_get_device_handle(user_context, buf));
    device_memcpy(user_context, c);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_hexagon_device_sync(void *user_context, struct buffer_t *) {
    debug(user_context)
        << "Hexagon: halide_hexagon_device_sync (user_context: " << user_context << ")\n";
    // Nothing to do.
    return 0;
}

WEAK int halide_hexagon_wrap_device_handle(void *user_context, struct buffer_t *buf,
                                           void *ion_buf, size_t size) {
    halide_assert(user_context, buf->dev == 0);
    if (buf->dev != 0) {
        return -2;
    }

    ion_device_handle *handle = new ion_device_handle();
    if (!handle) {
        return -1;
    }
    handle->buffer = ion_buf;
    handle->size = size;
    buf->dev = halide_new_device_wrapper(reinterpret<uint64_t>(handle), &hexagon_device_interface);
    if (buf->dev == 0) {
        delete handle;
        return -1;
    }
    return 0;
}

WEAK void *halide_hexagon_detach_device_handle(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return NULL;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &hexagon_device_interface);
    ion_device_handle *handle = reinterpret<ion_device_handle *>(halide_get_device_handle(buf->dev));
    void *ion_buf = handle->buffer;
    delete handle;

    halide_delete_device_wrapper(buf->dev);
    buf->dev = 0;
    return ion_buf;
}

WEAK void *halide_hexagon_get_device_handle(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return NULL;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &hexagon_device_interface);
    ion_device_handle *handle = reinterpret<ion_device_handle *>(halide_get_device_handle(buf->dev));
    return handle->buffer;
}

WEAK size_t halide_hexagon_get_device_size(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return 0;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &hexagon_device_interface);
    ion_device_handle *handle = reinterpret<ion_device_handle *>(halide_get_device_handle(buf->dev));
    return handle->size;
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
