#include "bin/src/halide_hexagon_remote.h"
#include <HalideRuntime.h>

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <hexagon_standalone.h>

#include "sim_protocol.h"
#include "log.h"
#include "elf.h"

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;

const int hvx_alignment = 128;

extern "C" {

// Provide an implementation of qurt to redirect to the appropriate
// simulator calls.
int qurt_hvx_lock(int mode) {
    SIM_ACQUIRE_HVX;
    if (mode == 0) {
        SIM_CLEAR_HVX_DOUBLE_MODE;
    } else {
        SIM_SET_HVX_DOUBLE_MODE;
    }
    return 0;
}

int qurt_hvx_unlock() {
    SIM_RELEASE_HVX;
    return 0;
}

// More symbols we need to support from halide_get_symbol.
extern int __hexagon_muldf3;
extern int __hexagon_divdf3;
extern int __hexagon_adddf3;
extern int __hexagon_divsf3;
extern int __hexagon_udivdi3;
extern int __hexagon_udivsi3;
extern int __hexagon_umodsi3;
extern int __hexagon_divsi3;
extern int __hexagon_modsi3;
extern int __hexagon_subdf3;
extern int sqrtf;
extern int sqrt;
extern int expf;
extern int exp;
extern int logf;
extern int log;
extern int powf;
extern int pow;
extern int sinf;
extern int sin;
extern int cosf;
extern int cos;
extern int tanf;
extern int tan;
extern int asinf;
extern int asin;
extern int acosf;
extern int acos;
extern int atanf;
extern int atan;
extern int atan2f;
extern int atan2;
extern int sinhf;
extern int sinh;
extern int coshf;
extern int cosh;
extern int tanhf;
extern int tanh;
extern int asinhf;
extern int asinh;
extern int acoshf;
extern int acosh;
extern int atanhf;
extern int atanh;
extern int nearbyintf;
extern int nearbyint;
extern int truncf;
extern int trunc;
extern int floorf;
extern int floor;
extern int ceilf;
extern int ceil;

// These might not always be available.
__attribute__((weak)) extern int mmap;
__attribute__((weak)) extern int mprotect;
__attribute__((weak)) extern int munmap;

}  // extern "C"

void log_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void halide_print(void *user_context, const char *str) {
    log_printf("%s", str);
}

// This is a basic implementation of the Halide runtime for Hexagon.
void halide_error(void *user_context, const char *str) {
    halide_print(user_context, str);
}

void *halide_malloc(void *user_context, size_t x) {
    return memalign(hvx_alignment, x);
}

void halide_free(void *user_context, void *ptr) {
    free(ptr);
}

int halide_do_task(void *user_context, halide_task_t f, int idx,
                   uint8_t *closure) {
    return f(user_context, idx, closure);
}

int halide_do_par_for(void *user_context, halide_task_t f,
                      int min, int size, uint8_t *closure) {
    for (int x = min; x < min + size; x++) {
        int result = halide_do_task(user_context, f, x, closure);
        if (result) {
            return result;
        }
    }
    return 0;
}

void halide_mutex_destroy(halide_mutex *) {}

void *halide_get_symbol(const char *name) {
    // dlsym is just a stub on the simulator, so we need to support
    // the symbols we need manually.
    struct known_sym {
        const char *name;
        char *addr;
    };
    static known_sym known_syms[] = {
        {"close", (char *)(&close)},
        {"abort", (char *)(&abort)},
        {"memcpy", (char *)(&memcpy)},
        {"memmove", (char *)(&memmove)},
        {"memset", (char *)(&memset)},
        {"halide_mutex_destroy", (char *)(&halide_mutex_destroy)},
        {"halide_profiler_get_state", (char *)(&halide_profiler_get_state)},
        {"qurt_hvx_lock", (char *)(&qurt_hvx_lock)},
        {"qurt_hvx_unlock", (char *)(&qurt_hvx_unlock)},
        {"__hexagon_divdf3", (char *)(&__hexagon_divdf3)},
        {"__hexagon_muldf3", (char *)(&__hexagon_muldf3)},
        {"__hexagon_adddf3", (char *)(&__hexagon_adddf3)},
        {"__hexagon_subdf3", (char *)(&__hexagon_subdf3)},
        {"__hexagon_divsf3", (char *)(&__hexagon_divsf3)},
        {"__hexagon_udivdi3", (char *)(&__hexagon_udivdi3)},
        {"__hexagon_udivsi3", (char *)(&__hexagon_udivsi3)},
        {"__hexagon_umodsi3", (char *)(&__hexagon_umodsi3)},
        {"__hexagon_divsi3", (char *)(&__hexagon_divsi3)},
        {"__hexagon_modsi3", (char *)(&__hexagon_modsi3)},
        {"__hexagon_sqrtf", (char *)(&sqrtf)},
        {"sqrtf", (char *)(&sqrtf)},
        {"sqrt", (char *)(&sqrt)},
        {"sinf", (char *)(&sinf)},
        {"expf", (char *)(&expf)},
        {"exp", (char *)(&exp)},
        {"logf", (char *)(&logf)},
        {"log", (char *)(&log)},
        {"powf", (char *)(&powf)},
        {"pow", (char *)(&pow)},
        {"sin", (char *)(&sin)},
        {"cosf", (char *)(&cosf)},
        {"cos", (char *)(&cos)},
        {"tanf", (char *)(&tanf)},
        {"tan", (char *)(&tan)},
        {"asinf", (char *)(&asinf)},
        {"asin", (char *)(&asin)},
        {"acosf", (char *)(&acosf)},
        {"acos", (char *)(&acos)},
        {"atanf", (char *)(&atanf)},
        {"atan", (char *)(&atan)},
        {"atan2f", (char *)(&atan2f)},
        {"atan2", (char *)(&atan2)},
        {"sinhf", (char *)(&sinhf)},
        {"sinh", (char *)(&sinh)},
        {"coshf", (char *)(&coshf)},
        {"cosh", (char *)(&cosh)},
        {"tanhf", (char *)(&tanhf)},
        {"tanh", (char *)(&tanh)},
        {"asinhf", (char *)(&asinhf)},
        {"asinh", (char *)(&asinh)},
        {"acoshf", (char *)(&acoshf)},
        {"acosh", (char *)(&acosh)},
        {"atanhf", (char *)(&atanhf)},
        {"atanh", (char *)(&atanh)},
        {"nearbyintf", (char *)(&nearbyintf)},
        {"nearbyint", (char *)(&nearbyint)},
        {"truncf", (char *)(&truncf)},
        {"trunc", (char *)(&trunc)},
        {"floorf", (char *)(&floorf)},
        {"floor", (char *)(&floor)},
        {"ceilf", (char *)(&ceilf)},
        {"ceil", (char *)(&ceil)},
        {"mmap", (char *)(&mmap)},
        {"mprotect", (char *)(&mprotect)},
        {"munmap", (char *)(&munmap)},
        {NULL, NULL} // Null terminator.
    };

    for (int i = 0; known_syms[i].name; i++) {
        if (strncmp(name, known_syms[i].name, strlen(known_syms[i].name)+1) == 0) {
            return known_syms[i].addr;
        }
    }

    return dlsym(RTLD_DEFAULT, name);
}

void *halide_load_library(const char *name) {
    return dlopen(name, RTLD_LAZY);
}

void *halide_get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

typedef int (*set_runtime_t)(halide_malloc_t user_malloc,
                             halide_free_t custom_free,
                             halide_print_t print,
                             halide_error_handler_t error_handler,
                             halide_do_par_for_t do_par_for,
                             halide_do_task_t do_task,
                             void *(*)(const char *),
                             void *(*)(const char *),
                             void *(*)(void *, const char *));
void* dlopenbuf(const char*filename, const char* data, int size, int perms) __attribute__ ((weak));

static void dllib_init() {
    // The simulator needs this call to enable dlopen to work...
    const char *builtin[] = {"libgcc.so", "libc.so", "libstdc++.so"};
    dlinit(3, const_cast<char**>(builtin));
}

int initialize_kernels_v2(const unsigned char *code, int codeLen,
                          bool use_shared_object,
                          handle_t *module_ptr) {

    // Keep this false, even for dlbuf usage, as we do not yet have api for dlopenbuf from standalone.
    void *lib = NULL;
    elf_t *elib = NULL;
    if (use_shared_object) {

        // Create a unique file name
        char *newf = tmpnam(NULL);
        char filename[260];
        strcpy(filename, newf);
        strcat(filename, "_halide_hexagon_kernels");
        strcat(filename, ".so");

        // Open the library
        dllib_init();
        if(&dlopenbuf == NULL) {
            halide_print(NULL, "dlopenbuf missing\n");
            return -1;
        }
        lib = dlopenbuf( filename, (const char*)code, codeLen, RTLD_LOCAL | RTLD_LAZY);
        if (!lib) {
            halide_print(NULL, "dlopenbuf failed\n");
            return -1;
        }
    } else {
        elib = obj_dlopen_mem(code, codeLen);
        if (!elib) {
            halide_print(NULL, "dlopen_mem failed\n");
            return -1;
        }
    }
    // Initialize the runtime. The Hexagon runtime can't call any
    // system functions (because we can't link them), so we put all
    // the implementations that need to do so here, and pass poiners
    // to them in here.
    set_runtime_t set_runtime;
    if (use_shared_object) {
        set_runtime = (set_runtime_t)dlsym(lib, "halide_noos_set_runtime");
    } else {
        set_runtime = (set_runtime_t)obj_dlsym(elib, "halide_noos_set_runtime");
    }
    if (!set_runtime) {
        if (use_shared_object) {
            dlclose(lib);
        } else {
            obj_dlclose(elib);
        }
        halide_print(NULL, "halide_noos_set_runtime not found in shared object\n");
        return -1;
    }

    int result = set_runtime(halide_malloc,
                             halide_free,
                             halide_print,
                             halide_error,
                             halide_do_par_for,
                             halide_do_task,
                             halide_get_symbol,
                             halide_load_library,
                             halide_get_library_symbol);
    if (result != 0) {
        if (use_shared_object) {
            dlclose(lib);
        } else {
            obj_dlclose(elib);
        }
        halide_print(NULL, "set_runtime failed\n");
        return result;
    }
    if (use_shared_object) {
        *module_ptr = reinterpret_cast<handle_t>(lib);
    } else {
        *module_ptr = reinterpret_cast<handle_t>(elib);
    }
    return 0;
}

int initialize_kernels(const unsigned char *code, int codeLen,
                       bool use_shared_object,
                       handle_t *module_ptr) {
    return initialize_kernels_v2(code, codeLen, false, module_ptr);
}

handle_t get_symbol(handle_t module_ptr, const char* name, int nameLen, int use_shared_object) {
    if (use_shared_object) {
        return reinterpret_cast<handle_t>(dlsym(reinterpret_cast<elf_t*>(module_ptr), name));
    } else {
        return reinterpret_cast<handle_t>(obj_dlsym(reinterpret_cast<elf_t*>(module_ptr), name));
    }
}

int run(handle_t module_ptr, handle_t function,
        const buffer *input_buffersPtrs, int input_buffersLen,
        buffer *output_buffersPtrs, int output_buffersLen,
        const buffer *input_scalarsPtrs, int input_scalarsLen) {
    // Get a pointer to the argv version of the pipeline.
    typedef int (*pipeline_argv_t)(void **);
    pipeline_argv_t pipeline = reinterpret_cast<pipeline_argv_t>(function);

    // Construct a list of arguments. This is only part of a
    // buffer_t. We know that the only field of buffer_t that the
    // generated code should access is the host field (any other
    // fields should be passed as their own scalar parameters) so we
    // can just make this dummy buffer_t type.
    struct buffer_t {
        uint64_t dev;
        uint8_t* host;
    };
    void **args = (void **)__builtin_alloca((input_buffersLen + input_scalarsLen + output_buffersLen) * sizeof(void *));
    buffer_t *buffers = (buffer_t *)__builtin_alloca((input_buffersLen + output_buffersLen) * sizeof(buffer_t));

    void **next_arg = &args[0];
    buffer_t *next_buffer_t = &buffers[0];
    // Input buffers come first.
    for (int i = 0; i < input_buffersLen; i++, next_arg++, next_buffer_t++) {
        next_buffer_t->host = input_buffersPtrs[i].data;
        *next_arg = next_buffer_t;
    }
    // Output buffers are next.
    for (int i = 0; i < output_buffersLen; i++, next_arg++, next_buffer_t++) {
        next_buffer_t->host = output_buffersPtrs[i].data;
        *next_arg = next_buffer_t;
    }
    // Input scalars are last.
    for (int i = 0; i < input_scalarsLen; i++, next_arg++) {
        *next_arg = input_scalarsPtrs[i].data;
    }

    // Call the pipeline and return the result.
    return pipeline(args);
}

int release_kernels(handle_t module_ptr, int codeLen) {
    obj_dlclose(reinterpret_cast<elf_t*>(module_ptr));
    return 0;
}

extern "C" {
halide_profiler_state profiler_state;
int *profiler_current_func_addr = &profiler_state.current_func;
}

halide_profiler_state *halide_profiler_get_state() {
    return (halide_profiler_state *)(&profiler_state);
}

extern "C" {

// The global symbols with which we pass RPC commands and results.
volatile int rpc_call = Message::None;

// TODO: It would be better to use an array here (obviously), but I
// couldn't figure out how to write to the array from the simulator
// host side.
volatile int rpc_arg0;
volatile int rpc_arg1;
volatile int rpc_arg2;
volatile int rpc_arg3;
volatile int rpc_arg4;
volatile int rpc_arg5;
volatile int rpc_arg6;
volatile int rpc_arg7;
#define RPC_ARG(i) rpc_arg##i

volatile int rpc_ret = 0;

void set_rpc_return(int value) {
    rpc_ret = value;
    rpc_call = Message::None;
}

}

int main(int argc, const char **argv) {

    while(true) {
        switch (rpc_call) {
        case Message::None:
            break;
        case Message::Alloc:
            set_rpc_return(reinterpret_cast<int>(memalign(hvx_alignment, RPC_ARG(0))));
            break;
        case Message::Free:
            free(reinterpret_cast<void*>(RPC_ARG(0)));
            set_rpc_return(0);
            break;
        case Message::InitKernels:
            set_rpc_return(initialize_kernels_v2(
                reinterpret_cast<unsigned char*>(RPC_ARG(0)),
                RPC_ARG(1),
                RPC_ARG(2),
                reinterpret_cast<handle_t*>(RPC_ARG(3))));
            break;
        case Message::GetSymbol:
            set_rpc_return(get_symbol(
                static_cast<handle_t>(RPC_ARG(0)),
                reinterpret_cast<const char *>(RPC_ARG(1)),
                RPC_ARG(2),
                RPC_ARG(3)));
            break;
        case Message::Run:
            set_rpc_return(run(
                static_cast<handle_t>(RPC_ARG(0)),
                static_cast<handle_t>(RPC_ARG(1)),
                reinterpret_cast<const buffer*>(RPC_ARG(2)),
                RPC_ARG(3),
                reinterpret_cast<buffer*>(RPC_ARG(4)),
                RPC_ARG(5),
                reinterpret_cast<const buffer*>(RPC_ARG(6)),
                RPC_ARG(7)));
            break;
        case Message::ReleaseKernels:
            set_rpc_return(release_kernels(
                static_cast<handle_t>(RPC_ARG(0)),
                RPC_ARG(1)));
            break;
        case Message::Break:
            return 0;
        default:
            log_printf("Unknown message: %d\n", rpc_call);
            return -1;
        }
    }
    log_printf("Unreachable!\n");
    return 0;
}
