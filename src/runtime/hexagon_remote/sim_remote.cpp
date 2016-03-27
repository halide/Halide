#include "sim_protocol.h"
#include "../HalideRuntime.h"

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

typedef struct _buffer__seq_octet _buffer__seq_octet;
typedef _buffer__seq_octet buffer;
struct _buffer__seq_octet {
   unsigned char* data;
   int dataLen;
};
typedef unsigned int handle_t;

typedef handle_t handle_t;

// This is a basic implementation of the Halide runtime for Hexagon.
void halide_error(void *user_context, const char *str) {
    halide_print(user_context, str);
}

void *halide_malloc(void *user_context, size_t x) {
    // Allocate enough space for aligning the pointer we return.
    const size_t alignment = 128;
    void *orig = malloc(x + alignment);
    if (orig == NULL) {
        // Will result in a failed assertion and a call to halide_error
        return NULL;
    }
    // We want to store the original pointer prior to the pointer we return.
    void *ptr = (void *)(((size_t)orig + alignment + sizeof(void*) - 1) & ~(alignment - 1));
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void halide_free(void *user_context, void *ptr) {
    free(((void**)ptr)[-1]);
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

typedef int (*set_runtime_t)(halide_malloc_t user_malloc,
                             halide_free_t custom_free,
                             halide_print_t print,
                             halide_error_handler_t error_handler,
                             halide_do_par_for_t do_par_for,
                             halide_do_task_t do_task);

int initialize_kernels(const unsigned char *code, int codeLen,
                       handle_t *module_ptr) {
#if 1  // Use shared object from file
    const char *filename = (const char *)code;
#else
    const char *filename = "/data/halide_kernels.so";
    FILE* fd = fopen(filename, "w");
    if (!fd) {
        halide_print(NULL, "fopen failed\n");
        return -1;
    }

    fwrite(code, codeLen, 1, fd);
    fclose(fd);
#endif
    halide_print(NULL, "dlopen ");
    halide_print(NULL, filename);
    halide_print(NULL, "\n");
    void *lib = dlopen(filename, RTLD_LOCAL | RTLD_LAZY);
    if (!lib) {
        halide_print(NULL, "dlopen failed\n");
        halide_print(NULL, dlerror());
        return -1;
    }
    halide_print(NULL, "dlopen succeeded!\n");

    // Initialize the runtime. The Hexagon runtime can't call any
    // system functions (because we can't link them), so we put all
    // the implementations that need to do so here, and pass poiners
    // to them in here.
    set_runtime_t set_runtime = (set_runtime_t)dlsym(lib, "halide_noos_set_runtime");
    if (!set_runtime) {
        dlclose(lib);
        halide_print(NULL, "halide_noos_set_runtime not found in shared object\n");
        return -1;
    }

    int result = set_runtime(halide_malloc,
                             halide_free,
                             halide_print,
                             halide_error,
                             halide_do_par_for,
                             halide_do_task);
    if (result != 0) {
        dlclose(lib);
        halide_print(NULL, "set_runtime failed\n");
        return result;
    }
    *module_ptr = reinterpret_cast<handle_t>(lib);

    return 0;
}

handle_t get_symbol(handle_t module_ptr, const char* name, int nameLen) {
    return reinterpret_cast<handle_t>(dlsym(reinterpret_cast<void*>(module_ptr), name));
}

int run(handle_t module_ptr, handle_t function,
        const buffer *input_buffersPtrs, int input_buffersLen,
        const buffer *input_scalarsPtrs, int input_scalarsLen,
        buffer *output_buffersPtrs, int output_buffersLen) {
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
    // Input scalars are next.
    for (int i = 0; i < input_scalarsLen; i++, next_arg++) {
        *next_arg = input_scalarsPtrs[i].data;
    }
    // Output buffers are last.
    for (int i = 0; i < output_buffersLen; i++, next_arg++, next_buffer_t++) {
        next_buffer_t->host = output_buffersPtrs[i].data;
        *next_arg = next_buffer_t;
    }

    // Call the pipeline and return the result.
    return pipeline(args);
}

int release_kernels(handle_t module_ptr, int codeLen) {
    dlclose(reinterpret_cast<void*>(module_ptr));
    return 0;
}

extern "C" {

void halide_print(void *user_context, const char *str) {
    printf("%s", str);
}

// The global symbols with which we pass RPC commands and results.
volatile int rpc_call = Message::Break;

#if 0
volatile int rpc_args[16];
#define RPC_ARG(i) rpc_args[i]
#else
volatile int rpc_arg0;
volatile int rpc_arg1;
volatile int rpc_arg2;
volatile int rpc_arg3;
volatile int rpc_arg4;
volatile int rpc_arg5;
volatile int rpc_arg6;
volatile int rpc_arg7;
#define RPC_ARG(i) rpc_arg##i
#endif

volatile int rpc_ret = 0;

}

int main(int argc, const char **argv) {
    printf("Simulator loop running...\n");
    while(true) {
        fflush(stdout);
        switch (rpc_call) {
        case Message::None:
            break;
        case Message::Alloc:
            printf("Alloc(%d)\n", RPC_ARG(0));
            rpc_ret = reinterpret_cast<int>(halide_malloc(NULL, RPC_ARG(0)));
            break;
        case Message::Free:
            printf("Free\n");
            halide_free(NULL, reinterpret_cast<void*>(RPC_ARG(0)));
            rpc_ret = 0;
            break;
        case Message::InitKernels:
            printf("InitKernels\n");
            rpc_ret = initialize_kernels(
                reinterpret_cast<unsigned char*>(RPC_ARG(0)),
                RPC_ARG(1),
                reinterpret_cast<handle_t*>(RPC_ARG(2)));
            break;
        case Message::Run:
            printf("Run\n");
            rpc_ret = run(
                static_cast<handle_t>(RPC_ARG(0)),
                static_cast<handle_t>(RPC_ARG(1)),
                reinterpret_cast<const buffer*>(RPC_ARG(2)),
                RPC_ARG(3),
                reinterpret_cast<const buffer*>(RPC_ARG(4)),
                RPC_ARG(5),
                reinterpret_cast<buffer*>(RPC_ARG(6)),
                RPC_ARG(7));
            break;
        case Message::ReleaseKernels:
            printf("ReleaseKernels\n");
            rpc_ret = release_kernels(
                static_cast<handle_t>(RPC_ARG(0)),
                RPC_ARG(1));
            break;
        case Message::Break:
            printf("Break\n");
            return 0;
        default:
            printf("Unknown message!\n");
            return -1;
        }
        // Setting the message to zero indicates to the caller that
        // we're done processing the message.
        rpc_call = Message::None;
    }
    printf("Unreachable!\n");
    return 0;
}
