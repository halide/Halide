#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "HalideRuntimeHexagonHost.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Hexagon {

struct ion_device_handle {
    void *buffer;
    size_t size;
};

WEAK halide_mutex thread_lock = { { 0 } };

extern WEAK halide_device_interface_t hexagon_device_interface;

// Define dynamic version of hexagon_remote/halide_hexagon_remote.h
typedef struct _remote_buffer__seq_octet _remote_buffer__seq_octet;
typedef _remote_buffer__seq_octet remote_buffer;
struct _remote_buffer__seq_octet {
   unsigned char* data;
   int dataLen;
};

typedef int (*remote_initialize_kernels_v2_fn)(const unsigned char* codeptr,
                                               int codesize,
                                               int use_shared_object,
                                               halide_hexagon_handle_t*);
typedef int (*remote_initialize_kernels_fn)(const unsigned char* codeptr,
                                            int codesize,
                                            halide_hexagon_handle_t*);
typedef halide_hexagon_handle_t (*remote_get_symbol_v3_fn)(halide_hexagon_handle_t, const char*, int, int, halide_hexagon_handle_t*);
typedef halide_hexagon_handle_t (*remote_get_symbol_fn)(halide_hexagon_handle_t, const char*, int);
typedef int (*remote_run_fn)(halide_hexagon_handle_t, int,
                             const remote_buffer*, int, const remote_buffer*, int,
                             remote_buffer*, int);
typedef int (*remote_release_kernels_fn)(halide_hexagon_handle_t, int);
typedef int (*remote_poll_log_fn)(char *, int, int *);
typedef void (*remote_poll_profiler_state_fn)(int *, int *);
typedef int (*remote_power_fn)();
typedef int (*remote_power_mode_fn)(int);
typedef int (*remote_power_perf_fn)(int, unsigned int, unsigned int, int, unsigned int, unsigned int, int, int);

typedef void (*host_malloc_init_fn)();
typedef void *(*host_malloc_fn)(size_t);
typedef void (*host_free_fn)(void *);

WEAK remote_initialize_kernels_v2_fn remote_initialize_kernels_v2 = NULL;
WEAK remote_initialize_kernels_fn remote_initialize_kernels = NULL;
WEAK remote_get_symbol_v3_fn remote_get_symbol_v3 = NULL;
WEAK remote_get_symbol_fn remote_get_symbol = NULL;
WEAK remote_run_fn remote_run = NULL;
WEAK remote_release_kernels_fn remote_release_kernels = NULL;
WEAK remote_poll_log_fn remote_poll_log = NULL;
WEAK remote_poll_profiler_state_fn remote_poll_profiler_state = NULL;
WEAK remote_power_fn remote_power_hvx_on = NULL;
WEAK remote_power_fn remote_power_hvx_off = NULL;
WEAK remote_power_perf_fn remote_set_performance = NULL;
WEAK remote_power_mode_fn remote_set_performance_mode = NULL;

WEAK host_malloc_init_fn host_malloc_init = NULL;
WEAK host_malloc_init_fn host_malloc_deinit = NULL;
WEAK host_malloc_fn host_malloc = NULL;
WEAK host_free_fn host_free = NULL;

// This checks if there are any log messages available on the remote
// side. It should be called after every remote call.
WEAK void poll_log(void *user_context) {
    if (!remote_poll_log) return;

    while (true) {
        char message[1024];
        int read = 0;
        int result = remote_poll_log(&message[0], sizeof(message), &read);
        if (result != 0) {
            // Don't make this an error, otherwise we might obscure
            // more information about errors that would come later.
            print(user_context) << "Hexagon: remote_poll_log failed " << result << "\n";
            return;
        }

        if (read > 0) {
            halide_print(user_context, message);
        } else {
            break;
        }
    }
}

WEAK void get_remote_profiler_state(int *func, int *threads) {
    if (!remote_poll_profiler_state) {
        // This should only have been called if there's a remote profiler func installed.
        error(NULL) << "Hexagon: remote_poll_profiler_func not found\n";
    }

    remote_poll_profiler_state(func, threads);
}

template <typename T>
__attribute__((always_inline)) void get_symbol(void *user_context, void *host_lib, const char* name, T &sym, bool required = true) {
    debug(user_context) << "    halide_get_library_symbol('" << name << "') -> \n";
    sym = (T) halide_get_library_symbol(host_lib, name);
    debug(user_context) << "        " << (void *)sym << "\n";
    if (!sym && required) {
        error(user_context) << "Required Hexagon runtime symbol '" << name << "' not found.\n";
    }
}

// Load the hexagon remote runtime.
WEAK int init_hexagon_runtime(void *user_context) {
    if ((remote_initialize_kernels_v2 || remote_initialize_kernels)
         && remote_run && remote_release_kernels) {
        // Already loaded.
        return 0;
    }

    // The "support library" for Hexagon is essentially a way to delegate Hexagon
    // code execution based on the runtime; devices with Hexagon hardware will
    // simply provide conduits for execution on that hardware, while test/desktop/etc
    // environments can instead connect a simulator via the API.
    void *host_lib = halide_load_library("libhalide_hexagon_host.so");

    debug(user_context) << "Hexagon: init_hexagon_runtime (user_context: " << user_context << ")\n";

    // Get the symbols we need from the library.
    get_symbol(user_context, host_lib, "halide_hexagon_remote_initialize_kernels_v2", remote_initialize_kernels_v2, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_initialize_kernels", remote_initialize_kernels, /* required */ false);
    if (!remote_initialize_kernels_v2 && !remote_initialize_kernels) return -1;
    get_symbol(user_context, host_lib, "halide_hexagon_remote_get_symbol_v3", remote_get_symbol_v3, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_get_symbol", remote_get_symbol, /* required */ false);
    if (!remote_get_symbol && !remote_get_symbol_v3) return -1;
    get_symbol(user_context, host_lib, "halide_hexagon_remote_run", remote_run);
    if (!remote_run) return -1;
    get_symbol(user_context, host_lib, "halide_hexagon_remote_release_kernels", remote_release_kernels);
    if (!remote_release_kernels) return -1;

    get_symbol(user_context, host_lib, "halide_hexagon_host_malloc_init", host_malloc_init);
    if (!host_malloc_init) return -1;
    get_symbol(user_context, host_lib, "halide_hexagon_host_malloc_deinit", host_malloc_deinit);
    if (!host_malloc_deinit) return -1;
    get_symbol(user_context, host_lib, "halide_hexagon_host_malloc", host_malloc);
    if (!host_malloc) return -1;
    get_symbol(user_context, host_lib, "halide_hexagon_host_free", host_free);
    if (!host_free) return -1;

    // These symbols are optional.
    get_symbol(user_context, host_lib, "halide_hexagon_remote_poll_log", remote_poll_log, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_poll_profiler_state", remote_poll_profiler_state, /* required */ false);

    // If these are unavailable, then the runtime always powers HVX on and so these are not necessary.
    get_symbol(user_context, host_lib, "halide_hexagon_remote_power_hvx_on", remote_power_hvx_on, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_power_hvx_off", remote_power_hvx_off, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_set_performance", remote_set_performance, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_set_performance_mode", remote_set_performance_mode, /* required */ false);

    host_malloc_init();

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
using namespace Halide::Runtime::Internal::Hexagon;

extern "C" {

WEAK bool halide_is_hexagon_available(void *user_context) {
    int result = init_hexagon_runtime(user_context);
    return result == 0;
}

WEAK int halide_hexagon_initialize_kernels(void *user_context, void **state_ptr,
                                           const uint8_t *code, uint64_t code_size,
                                           uint32_t use_shared_object) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) return result;
    debug(user_context) << "Hexagon: halide_hexagon_initialize_kernels (user_context: " << user_context
                        << ", state_ptr: " << state_ptr
                        << ", *state_ptr: " << *state_ptr
                        << ", code: " << code
                        << ", code_size: " << (int)code_size
                        << ", use_shared_object: " << use_shared_object << ")\n";
    halide_assert(user_context, state_ptr != NULL);

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

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
        debug(user_context) << "    halide_remote_initialize_kernels -> ";
        halide_hexagon_handle_t module = 0;
        if (remote_initialize_kernels_v2) {
            result = remote_initialize_kernels_v2(code, code_size, use_shared_object, &module);
        } else {
            halide_assert(user_context, remote_initialize_kernels != NULL);
            if (use_shared_object) {
                error(user_context) << "Hexagon runtime does not support shared objects.\n";
                return -1;
            }
            result = remote_initialize_kernels(code, code_size, &module);
        }
        poll_log(user_context);
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

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return result != 0 ? -1 : 0;
}
namespace {

// Prepare an array of remote_buffer arguments, mapping buffers if
// necessary. Only arguments with flags&flag_mask == flag_value are
// added to the mapped_args array. Returns the number of arguments
// mapped, or a negative number on error.
WEAK int map_arguments(void *user_context, int arg_count,
                       uint64_t arg_sizes[], void *args[], int arg_flags[], int flag_mask, int flag_value,
                       remote_buffer *mapped_args) {
    int mapped_count = 0;
    for (int i = 0; i < arg_count; i++) {
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
                            uint32_t use_shared_object,
                            void *state_ptr,
                            const char *name,
                            halide_hexagon_handle_t* function,
                            uint64_t arg_sizes[],
                            void *args[],
                            int arg_flags[]) {
    halide_assert(user_context, state_ptr != NULL);
    halide_assert(user_context, function != NULL);
    int result = init_hexagon_runtime(user_context);
    if (result != 0) return result;

    halide_hexagon_handle_t module = state_ptr ? ((module_state *)state_ptr)->module : 0;
    debug(user_context) << "Hexagon: halide_hexagon_run ("
                        << "use_shared_object: " << use_shared_object << ", "
                        << "user_context: " << user_context << ", "
                        << "state_ptr: " << state_ptr << " (" << module << "), "
                        << "name: " << name << ", "
                        << "function: " << function << " (" << *function << "))\n";

    // If we haven't gotten the symbol for this function, do so now.
    if (*function == 0) {
        debug(user_context) << "    halide_hexagon_remote_get_symbol" << name << " -> ";
        if (remote_get_symbol_v3) {
            halide_hexagon_handle_t sym = 0;
            int result = remote_get_symbol_v3(module, name, strlen(name) + 1, use_shared_object, &sym);
            *function = result == 0 ? sym : 0;
        } else {
            halide_assert(user_context, remote_get_symbol != NULL);
            *function = remote_get_symbol(module, name, strlen(name) + 1);
        }
        poll_log(user_context);
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

    // If remote profiling is supported, tell the profiler to call
    // get_remote_profiler_func to retrieve the current
    // func. Otherwise leave it alone - the cost of remote running
    // will be billed to the calling Func.
    if (remote_poll_profiler_state) {
        halide_profiler_get_state()->get_remote_profiler_state = get_remote_profiler_state;
    }

    // Call the pipeline on the device side.
    debug(user_context) << "    halide_hexagon_remote_run -> ";
    result = remote_run(module, *function,
                        input_buffers, input_buffer_count,
                        output_buffers, output_buffer_count,
                        input_scalars, input_scalar_count);
    poll_log(user_context);
    debug(user_context) << "        " << result << "\n";
    if (result != 0) {
        error(user_context) << "Hexagon pipeline failed.\n";
        return result;
    }

    halide_profiler_get_state()->get_remote_profiler_state = NULL;

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
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
            poll_log(user_context);
            debug(user_context) << "        " << result << "\n";
            state->module = 0;
            state->size = 0;
        }
        state = state->next;
    }
    state_list = NULL;

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

    size_t size = buf_size(buf);
    halide_assert(user_context, size != 0);

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
        debug(user_context) << "    host_malloc len=" << (uint64_t)size << " -> ";
        ion = host_malloc(size);
        debug(user_context) << "        " << ion << "\n";
        if (!ion) {
            error(user_context) << "host_malloc failed\n";
            return -1;
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
        if (size >= min_ion_allocation_size) {
            host_free(ion);
        } else {
            halide_free(user_context, ion);
        }
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
        debug(user_context) << "    host_free ion=" << ion << "\n";
        host_free(ion);
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
    c.copy_memory(user_context);

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
    c.copy_memory(user_context);

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
                                           void *ion_buf, uint64_t size) {
    halide_assert(user_context, buf->dev == 0);
    if (buf->dev != 0) {
        return -2;
    }

    ion_device_handle *handle = (ion_device_handle*)  halide_malloc(user_context, sizeof(ion_device_handle));
    if (!handle) {
        return -1;
    }
    handle->buffer = ion_buf;
    handle->size = size;
    buf->dev = halide_new_device_wrapper(reinterpret<uint64_t>(handle), &hexagon_device_interface);
    if (buf->dev == 0) {
        halide_free(user_context, handle);
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
    halide_free(user_context, handle);

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

WEAK uint64_t halide_hexagon_get_device_size(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return 0;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &hexagon_device_interface);
    ion_device_handle *handle = reinterpret<ion_device_handle *>(halide_get_device_handle(buf->dev));
    return handle->size;
}

WEAK int halide_hexagon_device_and_host_malloc(void *user_context, struct buffer_t *buf) {
    debug(user_context) << "halide_hexagon_device_and_host_malloc called.\n";
    int result = halide_hexagon_device_malloc(user_context, buf);
    if (result == 0) {
        buf->host = (uint8_t *)halide_hexagon_get_device_handle(user_context, buf);
    }
    return result;
}

WEAK int halide_hexagon_device_and_host_free(void *user_context, struct buffer_t *buf) {
    debug(user_context) << "halide_hexagon_device_and_host_free called.\n";
    halide_hexagon_device_free(user_context, buf);
    buf->host = NULL;
    return 0;
}

WEAK int halide_hexagon_power_hvx_on(void *user_context) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) return result;

    debug(user_context) << "halide_hexagon_power_hvx_on\n";
    if (!remote_power_hvx_on) {
        // The function is not available in this version of the
        // runtime, this runtime always powers HVX on.
        return 0;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    debug(user_context) << "    remote_power_hvx_on -> ";
    result = remote_power_hvx_on();
    debug(user_context) << "        " << result << "\n";
    if (result != 0) {
        error(user_context) << "remote_power_hvx_on failed.\n";
        return result;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_hexagon_power_hvx_off(void *user_context) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) return result;

    debug(user_context) << "halide_hexagon_power_hvx_off\n";
    if (!remote_power_hvx_off) {
        // The function is not available in this version of the
        // runtime, this runtime always powers HVX on.
        return 0;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    debug(user_context) << "    remote_power_hvx_off -> ";
    result = remote_power_hvx_off();
    debug(user_context) << "        " << result << "\n";
    if (result != 0) {
        error(user_context) << "remote_power_hvx_off failed.\n";
        return result;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK void halide_hexagon_power_hvx_off_as_destructor(void *user_context, void * /* obj */) {
    halide_hexagon_power_hvx_off(user_context);
}

WEAK int halide_hexagon_set_performance_mode(void *user_context, halide_hvx_power_mode_t mode) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) return result;

    debug(user_context) << "halide_hexagon_set_performance_mode\n";
    if (!remote_set_performance_mode) {
        // This runtime doesn't support changing the performance target.
        return 0;
    }

    debug(user_context) << "    remote_set_performance_mode -> ";
    result = remote_set_performance_mode(mode);
    debug(user_context) << "        " << result << "\n";
    if (result != 0) {
        error(user_context) << "remote_set_performance_mode failed.\n";
        return result;
    }

    return 0;
}

WEAK int halide_hexagon_set_performance(void *user_context, halide_hvx_power_perf_t *perf) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) return result;

    debug(user_context) << "halide_hexagon_set_performance\n";
    if (!remote_set_performance) {
        // This runtime doesn't support changing the performance target.
        return 0;
    }

    debug(user_context) << "    remote_set_performance -> ";
    result = remote_set_performance(perf->set_mips,
                                    perf->mipsPerThread,
                                    perf->mipsTotal,
                                    perf->set_bus_bw,
                                    perf->bwMegabytesPerSec,
                                    perf->busbwUsagePercentage,
                                    perf->set_latency,
                                    perf->latency);

    debug(user_context) << "        " << result << "\n";
    if (result != 0) {
        error(user_context) << "remote_set_performance failed.\n";
        return result;
    }

    return 0;
}

WEAK const halide_device_interface_t *halide_hexagon_device_interface() {
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

WEAK halide_device_interface_t hexagon_device_interface = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_hexagon_device_malloc,
    halide_hexagon_device_free,
    halide_hexagon_device_sync,
    halide_hexagon_device_release,
    halide_hexagon_copy_to_host,
    halide_hexagon_copy_to_device,
    halide_hexagon_device_and_host_malloc,
    halide_hexagon_device_and_host_free,
};

}}}} // namespace Halide::Runtime::Internal::Hexagon
