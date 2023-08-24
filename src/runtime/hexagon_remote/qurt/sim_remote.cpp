#include "HalideRuntime.h"
#include "halide_hexagon_remote.h"

#include "hexagon_standalone.h"
#include <dlfcn.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "known_symbols.h"
#include "log.h"
#include "sim_protocol.h"

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;

const int hvx_alignment = 128;

// memalign() on the Simulator is unreliable and can apparently return
// overlapping areas. Roll our own version that is based on malloc().
static void *aligned_malloc(size_t alignment, size_t x) {
    void *orig = malloc(x + alignment);
    if (!orig) {
        return NULL;
    }
    // We want to store the original pointer prior to the pointer we return.
    void *ptr = (void *)(((size_t)orig + alignment + sizeof(void *) - 1) & ~(alignment - 1));
    ((void **)ptr)[-1] = orig;
    return ptr;
}

static void aligned_free(void *ptr) {
    if (ptr) {
        free(((void **)ptr)[-1]);
    }
}

void log_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

extern "C" {

void halide_print(void *user_context, const char *str) {
    log_printf("%s", str);
}

// This is a basic implementation of the Halide runtime for Hexagon.
void halide_error(void *user_context, const char *str) {
    halide_print(user_context, str);
}

void *halide_get_symbol(const char *name) {
    // dlsym doesn't do anything on the simulator, and we need to
    // support mmap/mprotect/mnmap if needed.
    return get_known_symbol(name);
}

void *halide_load_library(const char *name) {
    return dlopen(name, RTLD_LAZY);
}

void *halide_get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

__attribute__((weak)) void *dlopenbuf(const char *filename, const char *data, int size, int perms);

}  // extern "C"

static void dllib_init() {
    // The simulator needs this call to enable dlopen to work...
    const char *builtin[] = {"libgcc.so", "libc.so", "libstdc++.so"};
    dlinit(3, const_cast<char **>(builtin));
}

int load_library(const char *soname, const unsigned char *code, int codeLen, handle_t *module_ptr) {
    void *lib = NULL;
    if (!dlopenbuf) {
        log_printf("dlopenbuf not available.\n");
        return -1;
    }

    // Open the library
    dllib_init();
    // We need to use RTLD_NOW, the libraries we build for Hexagon
    // offloading do not support lazy bindin.
    lib = dlopenbuf(soname, (const char *)code, codeLen, RTLD_LOCAL | RTLD_NOW);
    if (!lib) {
        halide_print(NULL, "dlopenbuf failed\n");
        halide_print(NULL, dlerror());
        return -1;
    }

    *module_ptr = reinterpret_cast<handle_t>(lib);
    return 0;
}

handle_t get_symbol(handle_t module_ptr, const char *name, int nameLen) {
    return reinterpret_cast<handle_t>(dlsym(reinterpret_cast<void *>(module_ptr), name));
}

int run(handle_t module_ptr, handle_t function,
        const buffer *input_buffersPtrs, int input_buffersLen,
        buffer *output_buffersPtrs, int output_buffersLen,
        const buffer *input_scalarsPtrs, int input_scalarsLen) {
    // Get a pointer to the argv version of the pipeline.
    typedef int (*pipeline_argv_t)(void **);
    pipeline_argv_t pipeline = reinterpret_cast<pipeline_argv_t>(function);

    // Construct a list of arguments.
    struct hexagon_device_pointer {
        uint64_t dev;
        uint8_t *host;
    };

    size_t args_size = (input_buffersLen + input_scalarsLen + output_buffersLen) * sizeof(void *);
    size_t buffers_size = (input_buffersLen + output_buffersLen) * sizeof(hexagon_device_pointer);

    void **args = (void **)__builtin_alloca(args_size);
    hexagon_device_pointer *buffers = (hexagon_device_pointer *)__builtin_alloca(buffers_size);
    memset(buffers, 0, buffers_size);

    void **next_arg = &args[0];
    hexagon_device_pointer *next_buffer_t = &buffers[0];
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

int release_library(handle_t module_ptr) {
    dlclose(reinterpret_cast<void *>(module_ptr));
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

    while (true) {
        switch (rpc_call) {
        case Message::None:
            break;
        case Message::Alloc:
            set_rpc_return(reinterpret_cast<int>(aligned_malloc(hvx_alignment, RPC_ARG(0))));
            break;
        case Message::Free:
            aligned_free(reinterpret_cast<void *>(RPC_ARG(0)));
            set_rpc_return(0);
            break;
        case Message::LoadLibrary:
            set_rpc_return(load_library(
                reinterpret_cast<char *>(RPC_ARG(0)),
                reinterpret_cast<unsigned char *>(RPC_ARG(2)),
                RPC_ARG(3),
                reinterpret_cast<handle_t *>(RPC_ARG(4))));
            break;
        case Message::GetSymbol:
            set_rpc_return(get_symbol(
                static_cast<handle_t>(RPC_ARG(0)),
                reinterpret_cast<const char *>(RPC_ARG(1)),
                RPC_ARG(2)));
            break;
        case Message::Run:
            set_rpc_return(run(
                static_cast<handle_t>(RPC_ARG(0)),
                static_cast<handle_t>(RPC_ARG(1)),
                reinterpret_cast<const buffer *>(RPC_ARG(2)),
                RPC_ARG(3),
                reinterpret_cast<buffer *>(RPC_ARG(4)),
                RPC_ARG(5),
                reinterpret_cast<const buffer *>(RPC_ARG(6)),
                RPC_ARG(7)));
            break;
        case Message::ReleaseLibrary:
            set_rpc_return(release_library(
                static_cast<handle_t>(RPC_ARG(0))));
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
