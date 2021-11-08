#include "HalideRuntimeHexagonHost.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "runtime_internal.h"
#include "scoped_mutex_lock.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Hexagon {

struct ion_device_handle {
    void *buffer;
    size_t size;
};

WEAK halide_mutex thread_lock = {{0}};

extern WEAK halide_device_interface_t hexagon_device_interface;

// Define dynamic version of hexagon_remote/halide_hexagon_remote.h
typedef struct _remote_buffer__seq_octet _remote_buffer__seq_octet;
typedef _remote_buffer__seq_octet remote_buffer;
struct _remote_buffer__seq_octet {
    unsigned char *data;
    int dataLen;
};

typedef int (*remote_load_library_fn)(const char *, int, const unsigned char *, int, halide_hexagon_handle_t *);
typedef int (*remote_get_symbol_fn)(halide_hexagon_handle_t, const char *, int, halide_hexagon_handle_t *);
typedef int (*remote_run_fn)(halide_hexagon_handle_t, int,
                             const remote_buffer *, int, const remote_buffer *, int,
                             remote_buffer *, int);
typedef int (*remote_release_library_fn)(halide_hexagon_handle_t);
typedef int (*remote_poll_log_fn)(char *, int, int *);
typedef void (*remote_poll_profiler_state_fn)(int *, int *);
typedef int (*remote_profiler_set_current_func_fn)(int);
typedef int (*remote_power_fn)();
typedef int (*remote_power_mode_fn)(int);
typedef int (*remote_power_perf_fn)(int, unsigned int, unsigned int, int, unsigned int, unsigned int, int, int);
typedef int (*remote_thread_priority_fn)(int);

typedef void (*host_malloc_init_fn)();
typedef void *(*host_malloc_fn)(size_t);
typedef void (*host_free_fn)(void *);

WEAK remote_load_library_fn remote_load_library = nullptr;
WEAK remote_get_symbol_fn remote_get_symbol = nullptr;
WEAK remote_run_fn remote_run = nullptr;
WEAK remote_release_library_fn remote_release_library = nullptr;
WEAK remote_poll_log_fn remote_poll_log = nullptr;
WEAK remote_poll_profiler_state_fn remote_poll_profiler_state = nullptr;
WEAK remote_profiler_set_current_func_fn remote_profiler_set_current_func = nullptr;
WEAK remote_power_fn remote_power_hvx_on = nullptr;
WEAK remote_power_fn remote_power_hvx_off = nullptr;
WEAK remote_power_perf_fn remote_set_performance = nullptr;
WEAK remote_power_mode_fn remote_set_performance_mode = nullptr;
WEAK remote_thread_priority_fn remote_set_thread_priority = nullptr;

WEAK host_malloc_init_fn host_malloc_init = nullptr;
WEAK host_malloc_init_fn host_malloc_deinit = nullptr;
WEAK host_malloc_fn host_malloc = nullptr;
WEAK host_free_fn host_free = nullptr;

// This checks if there are any log messages available on the remote
// side. It should be called after every remote call.
WEAK void poll_log(void *user_context) {
    if (!remote_poll_log) {
        return;
    }

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
        error(nullptr) << "Hexagon: remote_poll_profiler_func not found\n";
    }

    remote_poll_profiler_state(func, threads);
}

template<typename T>
ALWAYS_INLINE T *uint64_to_ptr(const uint64_t &u) {
    return reinterpret_cast<T *>((uintptr_t)u);
}

template<typename T>
ALWAYS_INLINE uint64_t ptr_to_uint64(T *ptr) {
    return (uint64_t) reinterpret_cast<uintptr_t>(ptr);
}

template<typename T>
ALWAYS_INLINE void get_symbol(void *user_context, void *host_lib, const char *name, T &sym, bool required = true) {
    debug(user_context) << "    halide_get_library_symbol('" << name << "') -> \n";
    sym = (T)halide_get_library_symbol(host_lib, name);
    debug(user_context) << "        " << (void *)sym << "\n";
    if (!sym && required) {
        error(user_context) << "Required Hexagon runtime symbol '" << name << "' not found.\n";
    }
}

// Load the hexagon remote runtime.
WEAK int init_hexagon_runtime(void *user_context) {
    if (remote_load_library && remote_run && remote_release_library) {
        // Already loaded.
        return 0;
    }

    // The "support library" for Hexagon is essentially a way to delegate Hexagon
    // code execution based on the runtime; devices with Hexagon hardware will
    // simply provide conduits for execution on that hardware, while test/desktop/etc
    // environments can instead connect a simulator via the API.
    // Load the .so for Linux or Android, and if that fails try the .dll
    // as we may be running the windows hosted simulator.
    void *host_lib = halide_load_library("libhalide_hexagon_host.so");
    if (!host_lib) {
        host_lib = halide_load_library("libhalide_hexagon_host.dll");
    }

    debug(user_context) << "Hexagon: init_hexagon_runtime (user_context: " << user_context << ")\n";

    // Get the symbols we need from the library.
    get_symbol(user_context, host_lib, "halide_hexagon_remote_load_library", remote_load_library);
    if (!remote_load_library) {
        return -1;
    }
    get_symbol(user_context, host_lib, "halide_hexagon_remote_get_symbol_v4", remote_get_symbol);
    if (!remote_get_symbol) {
        return -1;
    }
    get_symbol(user_context, host_lib, "halide_hexagon_remote_run", remote_run);
    if (!remote_run) {
        return -1;
    }
    get_symbol(user_context, host_lib, "halide_hexagon_remote_release_library", remote_release_library);
    if (!remote_release_library) {
        return -1;
    }

    get_symbol(user_context, host_lib, "halide_hexagon_host_malloc_init", host_malloc_init);
    if (!host_malloc_init) {
        return -1;
    }
    get_symbol(user_context, host_lib, "halide_hexagon_host_malloc_deinit", host_malloc_deinit);
    if (!host_malloc_deinit) {
        return -1;
    }
    get_symbol(user_context, host_lib, "halide_hexagon_host_malloc", host_malloc);
    if (!host_malloc) {
        return -1;
    }
    get_symbol(user_context, host_lib, "halide_hexagon_host_free", host_free);
    if (!host_free) {
        return -1;
    }

    // These symbols are optional.
    get_symbol(user_context, host_lib, "halide_hexagon_remote_poll_log", remote_poll_log, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_poll_profiler_state", remote_poll_profiler_state, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_profiler_set_current_func", remote_profiler_set_current_func, /* required */ false);

    // If these are unavailable, then the runtime always powers HVX on and so these are not necessary.
    get_symbol(user_context, host_lib, "halide_hexagon_remote_power_hvx_on", remote_power_hvx_on, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_power_hvx_off", remote_power_hvx_off, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_set_performance", remote_set_performance, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_set_performance_mode", remote_set_performance_mode, /* required */ false);
    get_symbol(user_context, host_lib, "halide_hexagon_remote_set_thread_priority", remote_set_thread_priority, /* required */ false);

    host_malloc_init();

    return 0;
}

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    halide_hexagon_handle_t module;
    module_state *next;
};
WEAK module_state *state_list = nullptr;
WEAK halide_hexagon_handle_t shared_runtime = 0;

#ifdef DEBUG_RUNTIME

// In debug builds, we write shared objects to the current directory (without
// failing on errors).
WEAK void write_shared_object(void *user_context, const char *path,
                              const uint8_t *code, uint64_t code_size) {
    void *f = fopen(path, "wb");
    if (!f) {
        debug(user_context) << "    failed to write shared object to '" << path << "'\n";
        return;
    }
    size_t written = fwrite(code, 1, code_size, f);
    if (written != code_size) {
        debug(user_context) << "    bad write of shared object to '" << path << "'\n";
    }
    fclose(f);
}

#endif

}  // namespace Hexagon
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::Hexagon;

extern "C" {

WEAK bool halide_is_hexagon_available(void *user_context) {
    int result = init_hexagon_runtime(user_context);
    return result == 0;
}

WEAK int halide_hexagon_initialize_kernels(void *user_context, void **state_ptr,
                                           const uint8_t *code, uint64_t code_size,
                                           const uint8_t *runtime, uint64_t runtime_size) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) {
        return result;
    }
    debug(user_context) << "Hexagon: halide_hexagon_initialize_kernels (user_context: " << user_context
                        << ", state_ptr: " << state_ptr
                        << ", *state_ptr: " << *state_ptr
                        << ", code: " << code
                        << ", code_size: " << (int)code_size << ")\n"
                        << ", code: " << runtime
                        << ", code_size: " << (int)runtime_size << ")\n";
    halide_abort_if_false(user_context, state_ptr != nullptr);

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

    // Initialize the runtime, if necessary.
    if (!shared_runtime) {
        debug(user_context) << "    Initializing shared runtime\n";
        const char soname[] = "libhalide_shared_runtime.so";
#ifdef DEBUG_RUNTIME
        debug(user_context) << "    Writing shared object '" << soname << "'\n";
        write_shared_object(user_context, soname, runtime, runtime_size);
#endif
        debug(user_context) << "    halide_remote_load_library(" << soname << ") -> ";
        result = remote_load_library(soname, sizeof(soname), runtime, runtime_size, &shared_runtime);
        poll_log(user_context);
        if (result == 0) {
            debug(user_context) << "        " << (void *)(size_t)shared_runtime << "\n";
            halide_abort_if_false(user_context, shared_runtime != 0);
        } else {
            debug(user_context) << "        " << result << "\n";
            error(user_context) << "Initialization of Hexagon kernels failed\n";
            shared_runtime = 0;
        }
    } else {
        debug(user_context) << "    re-using existing shared runtime " << (void *)(size_t)shared_runtime << "\n";
    }

    if (result != 0) {
        return -1;
    }

    module_state **state = (module_state **)state_ptr;
    if (!(*state)) {
        debug(user_context) << "    allocating module state -> \n";
        *state = (module_state *)malloc(sizeof(module_state));
        debug(user_context) << "        " << *state << "\n";
        (*state)->module = 0;
        (*state)->next = state_list;
        state_list = *state;
    }

    // Create the module itself if necessary.
    if (!(*state)->module) {
        static int unique_id = 0;
        stringstream soname(user_context);
        soname << "libhalide_kernels" << unique_id++ << ".so";
#ifdef DEBUG_RUNTIME
        debug(user_context) << "    Writing shared object '" << soname.str() << "'\n";
        write_shared_object(user_context, soname.str(), code, code_size);
#endif
        debug(user_context) << "    halide_remote_load_library(" << soname.str() << ") -> ";
        halide_hexagon_handle_t module = 0;
        result = remote_load_library(soname.str(), soname.size() + 1, code, code_size, &module);
        poll_log(user_context);
        if (result == 0) {
            debug(user_context) << "        " << (void *)(size_t)module << "\n";
            (*state)->module = module;
        } else {
            debug(user_context) << "        " << result << "\n";
            error(user_context) << "Initialization of Hexagon kernels failed\n";
        }
    } else {
        debug(user_context) << "    re-using existing module " << (void *)(size_t)(*state)->module << "\n";
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return result != 0 ? -1 : 0;
}

WEAK void halide_hexagon_finalize_kernels(void *user_context, void *state_ptr) {
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
        if ((arg_flags[i] & flag_mask) != flag_value) {
            continue;
        }
        remote_buffer &mapped_arg = mapped_args[mapped_count++];
        if (arg_flags[i] != 0) {
            // This is the way that HexagonOffload packages arguments for us.
            struct hexagon_device_pointer {
                uint64_t dev;
                uint8_t *host;
            };
            const hexagon_device_pointer *b = (hexagon_device_pointer *)args[i];
            uint64_t device = b->dev;
            uint8_t *host = b->host;
            if (device) {
                // This argument has a device handle.
                ion_device_handle *ion_handle = uint64_to_ptr<ion_device_handle>(device);
                debug(user_context) << i << ", " << device << "\n";
                mapped_arg.data = reinterpret_cast<uint8_t *>(ion_handle->buffer);
                mapped_arg.dataLen = ion_handle->size;
            } else {
                // This is just a host buffer, and the size is passed in as the arg size.
                mapped_arg.data = host;
                mapped_arg.dataLen = arg_sizes[i];
            }
        } else {
            // This is a scalar, just put the pointer/size in the result.
            mapped_arg.data = (uint8_t *)args[i];
            mapped_arg.dataLen = arg_sizes[i];
        }
    }
    return mapped_count;
}

}  // namespace

WEAK int halide_hexagon_run(void *user_context,
                            void *state_ptr,
                            const char *name,
                            halide_hexagon_handle_t *function,
                            uint64_t arg_sizes[],
                            void *args[],
                            int arg_flags[]) {
    halide_abort_if_false(user_context, state_ptr != nullptr);
    halide_abort_if_false(user_context, function != nullptr);
    int result = init_hexagon_runtime(user_context);
    if (result != 0) {
        return result;
    }

    halide_hexagon_handle_t module = state_ptr ? ((module_state *)state_ptr)->module : 0;
    debug(user_context) << "Hexagon: halide_hexagon_run ("
                        << "user_context: " << user_context << ", "
                        << "state_ptr: " << state_ptr << " (" << module << "), "
                        << "name: " << name << ", "
                        << "function: " << function << " (" << *function << "))\n";

    // If we haven't gotten the symbol for this function, do so now.
    if (*function == 0) {
        debug(user_context) << "    halide_hexagon_remote_get_symbol " << name << " -> ";
        halide_hexagon_handle_t sym = 0;
        int result = remote_get_symbol(module, name, strlen(name) + 1, &sym);
        *function = result == 0 ? sym : 0;
        poll_log(user_context);
        debug(user_context) << "        " << *function << "\n";
        if (*function == 0) {
            error(user_context) << "Failed to find function " << name << " in module.\n";
            return -1;
        }
    }

    // Allocate some remote_buffer objects on the stack.
    int arg_count = 0;
    while (arg_sizes[arg_count] > 0) {
        arg_count++;
    }
    remote_buffer *mapped_buffers =
        (remote_buffer *)__builtin_alloca(arg_count * sizeof(remote_buffer));

    // Map the arguments.
    // First grab the input buffers (bit 0 of flags is set).
    remote_buffer *input_buffers = mapped_buffers;
    int input_buffer_count = map_arguments(user_context, arg_count, arg_sizes, args, arg_flags, 0x3, 0x1,
                                           input_buffers);
    if (input_buffer_count < 0) {
        return input_buffer_count;
    }

    // Then the output buffers (bit 1 of flags is set).
    remote_buffer *output_buffers = input_buffers + input_buffer_count;
    int output_buffer_count = map_arguments(user_context, arg_count, arg_sizes, args, arg_flags, 0x2, 0x2,
                                            output_buffers);
    if (output_buffer_count < 0) {
        return output_buffer_count;
    }

    // And the input scalars (neither bits 0 or 1 of flags is set).
    remote_buffer *input_scalars = output_buffers + output_buffer_count;
    int input_scalar_count = map_arguments(user_context, arg_count, arg_sizes, args, arg_flags, 0x3, 0x0,
                                           input_scalars);
    if (input_scalar_count < 0) {
        return input_scalar_count;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // If remote profiling is supported, tell the profiler to call
    // get_remote_profiler_func to retrieve the current
    // func. Otherwise leave it alone - the cost of remote running
    // will be billed to the calling Func.
    if (remote_poll_profiler_state) {
        halide_profiler_get_state()->get_remote_profiler_state = get_remote_profiler_state;
        if (remote_profiler_set_current_func) {
            remote_profiler_set_current_func(halide_profiler_get_state()->current_func);
        }
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

    halide_profiler_get_state()->get_remote_profiler_state = nullptr;

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return result != 0 ? -1 : 0;
}

WEAK int halide_hexagon_device_release(void *user_context) {
    debug(user_context)
        << "Hexagon: halide_hexagon_device_release (user_context: " << user_context << ")\n";

    ScopedMutexLock lock(&thread_lock);

    // Release all of the remote side modules.
    module_state *state = state_list;
    while (state) {
        if (state->module) {
            debug(user_context) << "    halide_remote_release_library " << state
                                << " (" << state->module << ") -> ";
            int result = remote_release_library(state->module);
            poll_log(user_context);
            debug(user_context) << "        " << result << "\n";
            state->module = 0;
        }
        state = state->next;
    }
    state_list = nullptr;

    if (shared_runtime) {
        debug(user_context) << "    releasing shared runtime\n";
        debug(user_context) << "    halide_remote_release_library " << shared_runtime << " -> ";
        int result = remote_release_library(shared_runtime);
        poll_log(user_context);
        debug(user_context) << "        " << result << "\n";
        shared_runtime = 0;
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

WEAK int halide_hexagon_device_malloc(void *user_context, halide_buffer_t *buf) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) {
        return result;
    }

    debug(user_context)
        << "Hexagon: halide_hexagon_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    if (buf->device) {
        // This buffer already has a device allocation
        return 0;
    }

    size_t size = buf->size_in_bytes();
    halide_abort_if_false(user_context, size != 0);

    // Hexagon code generation generates clamped ramp loads in a way
    // that requires up to an extra vector beyond the end of the
    // buffer to be legal to access.
    size += 128;

    for (int i = 0; i < buf->dimensions; i++) {
        halide_abort_if_false(user_context, buf->dim[i].stride >= 0);
    }

    debug(user_context) << "    allocating buffer of " << (uint64_t)size << " bytes\n";

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

WEAK int halide_hexagon_device_free(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_device_free (user_context: " << user_context
        << ", buf: " << buf << ")\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    uint64_t size = halide_hexagon_get_device_size(user_context, buf);
    void *ion = halide_hexagon_get_device_handle(user_context, buf);
    halide_hexagon_detach_device_handle(user_context, buf);
    if (size >= min_ion_allocation_size) {
        debug(user_context) << "    host_free ion=" << ion << "\n";
        host_free(ion);
    } else {
        debug(user_context) << "    halide_free ion=" << ion << "\n";
        halide_free(user_context, ion);
    }

    if (buf->host == ion) {
        // If we also set the host pointer, reset it.
        buf->host = nullptr;
        debug(user_context) << "    host <- 0x0\n";
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    // This is to match what the default implementation of halide_device_free does.
    buf->set_device_dirty(false);
    return 0;
}

WEAK int halide_hexagon_copy_to_device(void *user_context, halide_buffer_t *buf) {
    int err = halide_hexagon_device_malloc(user_context, buf);
    if (err) {
        return err;
    }

    debug(user_context)
        << "Hexagon: halide_hexagon_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ")\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    halide_abort_if_false(user_context, buf->host && buf->device);
    device_copy c = make_host_to_device_copy(buf);

    // Get the descriptor associated with the ion buffer.
    c.dst = ptr_to_uint64(halide_hexagon_get_device_handle(user_context, buf));
    copy_memory(c, user_context);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

WEAK int halide_hexagon_copy_to_host(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    halide_abort_if_false(user_context, buf->host && buf->device);
    device_copy c = make_device_to_host_copy(buf);

    // Get the descriptor associated with the ion buffer.
    c.src = ptr_to_uint64(halide_hexagon_get_device_handle(user_context, buf));
    copy_memory(c, user_context);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

WEAK int halide_hexagon_device_sync(void *user_context, struct halide_buffer_t *) {
    debug(user_context)
        << "Hexagon: halide_hexagon_device_sync (user_context: " << user_context << ")\n";
    // Nothing to do.
    return 0;
}

WEAK int halide_hexagon_wrap_device_handle(void *user_context, struct halide_buffer_t *buf,
                                           void *ion_buf, uint64_t size) {
    halide_abort_if_false(user_context, buf->device == 0);
    if (buf->device != 0) {
        return -2;
    }

    ion_device_handle *handle = (ion_device_handle *)malloc(sizeof(ion_device_handle));
    if (!handle) {
        return -1;
    }
    handle->buffer = ion_buf;
    handle->size = size;
    buf->device_interface = &hexagon_device_interface;
    buf->device_interface->impl->use_module();
    buf->device = ptr_to_uint64(handle);
    return 0;
}

WEAK int halide_hexagon_detach_device_handle(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == 0) {
        return 0;
    }
    halide_abort_if_false(user_context, buf->device_interface == &hexagon_device_interface);
    ion_device_handle *handle = uint64_to_ptr<ion_device_handle>(buf->device);
    free(handle);

    buf->device_interface->impl->release_module();
    buf->device = 0;
    buf->device_interface = nullptr;
    return 0;
}

WEAK void *halide_hexagon_get_device_handle(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == 0) {
        return nullptr;
    }
    halide_abort_if_false(user_context, buf->device_interface == &hexagon_device_interface);
    ion_device_handle *handle = uint64_to_ptr<ion_device_handle>(buf->device);
    return handle->buffer;
}

WEAK uint64_t halide_hexagon_get_device_size(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == 0) {
        return 0;
    }
    halide_abort_if_false(user_context, buf->device_interface == &hexagon_device_interface);
    ion_device_handle *handle = uint64_to_ptr<ion_device_handle>(buf->device);
    return handle->size;
}

WEAK int halide_hexagon_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context) << "halide_hexagon_device_and_host_malloc called.\n";
    int result = halide_hexagon_device_malloc(user_context, buf);
    if (result == 0) {
        buf->host = (uint8_t *)halide_hexagon_get_device_handle(user_context, buf);
    }
    return result;
}

WEAK int halide_hexagon_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context) << "halide_hexagon_device_and_host_free called.\n";
    halide_hexagon_device_free(user_context, buf);
    buf->host = nullptr;
    return 0;
}

WEAK int halide_hexagon_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                    const struct halide_device_interface_t *dst_device_interface,
                                    struct halide_buffer_t *dst) {
    // We only handle copies to hexagon buffers or to host
    halide_abort_if_false(user_context, dst_device_interface == nullptr ||
                                            dst_device_interface == &hexagon_device_interface);

    if ((src->device_dirty() || src->host == nullptr) &&
        src->device_interface != &hexagon_device_interface) {
        halide_abort_if_false(user_context, dst_device_interface == &hexagon_device_interface);
        // This is handled at the higher level.
        return halide_error_code_incompatible_device_interface;
    }

    bool from_host = (src->device_interface != &hexagon_device_interface) ||
                     (src->device == 0) ||
                     (src->host_dirty() && src->host != nullptr);
    bool to_host = !dst_device_interface;

    halide_abort_if_false(user_context, from_host || src->device);
    halide_abort_if_false(user_context, to_host || dst->device);

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    device_copy c = make_buffer_copy(src, from_host, dst, to_host);

    int err = 0;

    // Get the descriptor associated with the ion buffer.
    if (!from_host) {
        c.src = ptr_to_uint64(halide_hexagon_get_device_handle(user_context, src));
    }
    if (!to_host) {
        c.dst = ptr_to_uint64(halide_hexagon_get_device_handle(user_context, dst));
    }
    copy_memory(c, user_context);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return err;
}

namespace {

WEAK int hexagon_device_crop_from_offset(const struct halide_buffer_t *src, int64_t offset, struct halide_buffer_t *dst) {
    ion_device_handle *src_handle = (ion_device_handle *)src->device;
    ion_device_handle *dst_handle = (ion_device_handle *)malloc(sizeof(ion_device_handle));
    if (!dst_handle) {
        return halide_error_code_out_of_memory;
    }

    dst_handle->buffer = (uint8_t *)src_handle->buffer + offset;
    dst_handle->size = src_handle->size - offset;
    dst->device = ptr_to_uint64(dst_handle);
    dst->device_interface = src->device_interface;
    dst->set_device_dirty(src->device_dirty());
    return 0;
}

}  // namespace

WEAK int halide_hexagon_device_crop(void *user_context, const struct halide_buffer_t *src,
                                    struct halide_buffer_t *dst) {
    debug(user_context) << "halide_hexagon_device_crop called.\n";

    const int64_t offset = calc_device_crop_byte_offset(src, dst);
    return hexagon_device_crop_from_offset(src, offset, dst);
}

WEAK int halide_hexagon_device_slice(void *user_context, const struct halide_buffer_t *src,
                                     int slice_dim, int slice_pos, struct halide_buffer_t *dst) {
    debug(user_context) << "halide_hexagon_device_slice called.\n";

    const int64_t offset = calc_device_slice_byte_offset(src, slice_dim, slice_pos);
    return hexagon_device_crop_from_offset(src, offset, dst);
}

WEAK int halide_hexagon_device_release_crop(void *user_context, struct halide_buffer_t *dst) {
    debug(user_context) << "halide_hexagon_release_crop called\n";
    free((ion_device_handle *)dst->device);
    return 0;
}

WEAK int halide_hexagon_power_hvx_on(void *user_context) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) {
        return result;
    }

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
    if (result != 0) {
        return result;
    }

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

WEAK int halide_hexagon_set_performance_mode(void *user_context, halide_hexagon_power_mode_t mode) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) {
        return result;
    }

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

WEAK int halide_hexagon_set_performance(void *user_context, halide_hexagon_power_t *perf) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) {
        return result;
    }

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

WEAK int halide_hexagon_set_thread_priority(void *user_context, int priority) {
    int result = init_hexagon_runtime(user_context);
    if (result != 0) {
        return result;
    }

    debug(user_context) << "halide_hexagon_set_thread_priority\n";
    if (!remote_set_thread_priority) {
        // This runtime doesn't support changing the thread priority.
        return 0;
    }

    debug(user_context) << "    remote_set_thread_priority -> ";
    result = remote_set_thread_priority(priority);
    debug(user_context) << "        " << result << "\n";
    if (result != 0) {
        error(user_context) << "remote_set_thread_priority failed.\n";
        return result;
    }

    return 0;
}

WEAK const halide_device_interface_t *halide_hexagon_device_interface() {
    return &hexagon_device_interface;
}

namespace {
WEAK __attribute__((destructor)) void halide_hexagon_cleanup() {
    halide_hexagon_device_release(nullptr);
}
}  // namespace

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Hexagon {

WEAK halide_device_interface_impl_t hexagon_device_interface_impl = {
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
    halide_hexagon_buffer_copy,
    halide_hexagon_device_crop,
    halide_hexagon_device_slice,
    halide_hexagon_device_release_crop,
    halide_default_device_wrap_native,
    halide_default_device_detach_native,
};

WEAK halide_device_interface_t hexagon_device_interface = {
    halide_device_malloc,
    halide_device_free,
    halide_device_sync,
    halide_device_release,
    halide_copy_to_host,
    halide_copy_to_device,
    halide_device_and_host_malloc,
    halide_device_and_host_free,
    halide_buffer_copy,
    halide_device_crop,
    halide_device_slice,
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    nullptr,
    &hexagon_device_interface_impl};

}  // namespace Hexagon
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
