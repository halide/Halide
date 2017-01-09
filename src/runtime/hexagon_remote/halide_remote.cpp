#include "bin/src/halide_hexagon_remote.h"
#include <HalideRuntime.h>
#include <HalideRuntimeHexagonHost.h>

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <qurt.h>

extern "C" {
#include "HAP_farf.h"
#include "HAP_power.h"
}

#include "elf.h"
#include "pipeline_context.h"
#include "log.h"

const int stack_alignment = 128;
const int stack_size = 1024 * 1024;

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;

extern "C" {

// This is a basic implementation of the Halide runtime for Hexagon.
void halide_print(void *user_context, const char *str) {
    if (str) {
        log_printf("%s", str);
    }
}

void halide_error(void *user_context, const char *str) {
    if (!str) {
        log_printf("Unknown error\n");
    } else if (*str == '\0' || str[strlen(str) - 1] != '\n') {
        log_printf("Error: %s\n", str);
    } else {
        log_printf("Error: %s", str);
    }
}

namespace {

// We keep a small pool of small pre-allocated buffers for use by Halide
// code; some kernels end up doing per-scanline allocations and frees,
// which can cause a noticable performance impact on some workloads.
// 'num_buffers' is the number of pre-allocated buffers and 'buffer_size' is
// the size of each buffer. The pre-allocated buffers are shared among threads
// and we use __sync_val_compare_and_swap primitive to synchronize the buffer
// allocation.
// TODO(psuriana): make num_buffers configurable by user
const int num_buffers = 10;
const int buffer_size = 1024 * 64;
int buf_is_used[num_buffers];
char mem_buf[num_buffers][buffer_size]
    __attribute__((aligned(128))); /* Hexagon requires 128-byte alignment. */

}

void *halide_malloc(void *user_context, size_t x) {
    if (x <= buffer_size) {
        for (int i = 0; i < num_buffers; ++i) {
            if (__sync_val_compare_and_swap(buf_is_used+i, 0, 1) == 0) {
                return mem_buf[i];
            }
        }
    }
    return memalign(128, x);
}

void halide_free(void *user_context, void *ptr) {
    for (int i = 0; i < num_buffers; ++i) {
        if (mem_buf[i] == ptr) {
            buf_is_used[i] = 0;
            return;
        }
    }
    free(ptr);
}

void *halide_get_symbol(const char *name) {
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

PipelineContext run_context(stack_alignment, stack_size);

int halide_hexagon_remote_initialize_kernels(const unsigned char *code, int codeLen,
                                             handle_t *module_ptr) {
    elf_t *lib = obj_dlopen_mem(code, codeLen);
    if (!lib) {
        log_printf("dlopen failed");
        return -1;
    }

    // Initialize the runtime. The Hexagon runtime can't call any
    // system functions (because we can't link them), so we put all
    // the implementations that need to do so here, and pass poiners
    // to them in here.
    set_runtime_t set_runtime = (set_runtime_t)obj_dlsym(lib, "halide_noos_set_runtime");
    if (!set_runtime) {
        obj_dlclose(lib);
        log_printf("halide_noos_set_runtime not found in shared object\n");
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
        obj_dlclose(lib);
        log_printf("set_runtime failed (%d)\n", result);
        return result;
    }
    *module_ptr = reinterpret_cast<handle_t>(lib);

    return 0;
}

handle_t halide_hexagon_remote_get_symbol(handle_t module_ptr, const char* name, int nameLen) {
    return reinterpret_cast<handle_t>(obj_dlsym(reinterpret_cast<elf_t*>(module_ptr), name));
}

volatile int power_ref_count = 0;

int halide_hexagon_remote_power_hvx_on_perf(
    int set_mips,
    unsigned int mipsPerThread,
    unsigned int mipsTotal,
    int set_bus_bw,
    unsigned int bwMegabytesPerSec,
    unsigned int busbwUsagePercentage,
    int set_latency,
    int latency)
{
    if (power_ref_count == 0) {
        HAP_power_request_t request;

        request.type = HAP_power_set_apptype;
        request.apptype = HAP_POWER_COMPUTE_CLIENT_CLASS;
        int retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            log_printf("HAP_power_set(HAP_power_set_apptype) failed (%d)\n", retval);
            return -1;
        }

        request.type = HAP_power_set_HVX;
        request.hvx.power_up = TRUE;
        retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            log_printf("HAP_power_set(HAP_power_set_HVX) failed (%d)\n", retval);
            return -1;
        }

        request.type = HAP_power_set_mips_bw;
        request.mips_bw.set_mips        = set_mips;
        request.mips_bw.mipsPerThread   = mipsPerThread;
        request.mips_bw.mipsTotal       = mipsTotal;
        request.mips_bw.set_bus_bw      = set_bus_bw;
        request.mips_bw.bwBytePerSec    = ((uint64_t) bwMegabytesPerSec) << 20;
        request.mips_bw.busbwUsagePercentage = busbwUsagePercentage;
        request.mips_bw.set_latency     = set_latency;
        request.mips_bw.latency         = latency;
        retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            log_printf("HAP_power_set(HAP_power_set_mips_bw) failed (%d)\n", retval);
            return -1;
        }
    }
    power_ref_count++;
    return 0;
}

int halide_hexagon_remote_power_hvx_on_mode(int mode) {
    int set_mips = 0;
    unsigned int mipsPerThread = 0;
    unsigned int mipsTotal = 0;
    int set_bus_bw = 0;
    uint64_t bwBytePerSec = 0;
    unsigned int bwMegabytesPerSec = 0;
    unsigned int busbwUsagePercentage = 0;
    int set_latency = 0;
    int latency = 0;

    HAP_power_response_t power_info;
    unsigned int max_mips = 0;
    uint64 max_bus_bw = 0;

    power_info.type = HAP_power_get_max_mips;
    int retval = HAP_power_get(NULL, &power_info);
    if (0 != retval) {
        log_printf("HAP_power_get(HAP_power_get_max_mips) failed (%d)\n", retval);
        return -1;
    }
    max_mips = power_info.max_mips;

    // Make sure max_mips is at least sanity_mips
    const unsigned int sanity_mips = 500;
    if (max_mips < sanity_mips) {
        max_mips = sanity_mips;
    }

    power_info.type = HAP_power_get_max_bus_bw;
    retval = HAP_power_get(NULL, &power_info);
    if (0 != retval) {
        log_printf("HAP_power_get(HAP_power_get_max_bus_bw) failed (%d)\n", retval);
        return -1;
    }
    max_bus_bw = power_info.max_bus_bw;

    // The above API under-reports the max bus bw. If we use it as
    // reported, performance is bad. Experimentally, this only
    // needs to be ~10x.
    // Make sure max_bus_bw is at least sanity_bw
    const uint64 sanity_bw = 1000000000ULL;
    if (max_bus_bw < sanity_bw) {
        if (max_bus_bw == 0) {
            max_bus_bw = sanity_bw;
        }
        while (max_bus_bw < sanity_bw) {
            max_bus_bw <<= 3;  // Increase value while preserving bits
        }
    }

    set_mips    = TRUE;
    set_bus_bw  = TRUE;
    set_latency = TRUE;
    switch (mode) {
        case halide_hvx_power_low:
            mipsPerThread          = max_mips / 4;
            bwBytePerSec           = max_bus_bw / 2;
            busbwUsagePercentage   = 25;
            break;
        case halide_hvx_power_nominal:
            mipsPerThread          = (3 * max_mips) / 8;
            bwBytePerSec           = max_bus_bw;
            busbwUsagePercentage   = 50;
            break;
        case halide_hvx_power_turbo:
        default: 
            mipsPerThread          = max_mips;
            bwBytePerSec           = max_bus_bw * 4;
            busbwUsagePercentage   = 100;
            break;
    }
    mipsTotal = mipsPerThread * 2;
    latency = 1;

    bwMegabytesPerSec = bwBytePerSec >> 20;
    return halide_hexagon_remote_power_hvx_on_perf(
                                    set_mips,
                                    mipsPerThread,
                                    mipsTotal,
                                    set_bus_bw,
                                    bwMegabytesPerSec,
                                    busbwUsagePercentage,
                                    set_latency,
                                    latency);
}

int halide_hexagon_remote_power_hvx_on() {
    return halide_hexagon_remote_power_hvx_on_mode(halide_hvx_power_turbo);
}

int halide_hexagon_remote_power_hvx_off() {
    power_ref_count--;
    if (power_ref_count == 0) {
        HAP_power_request_t request;

        request.type = HAP_power_set_HVX;
        request.hvx.power_up = FALSE;
        int retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            log_printf("HAP_power_set(HAP_power_set_HVX) failed (%d)\n", retval);
            return -1;
        }

        request.type = HAP_power_set_mips_bw;
        request.mips_bw.set_mips        = TRUE;
        request.mips_bw.mipsPerThread   = 0;
        request.mips_bw.mipsTotal       = 0;
        request.mips_bw.set_bus_bw      = TRUE;
        request.mips_bw.bwBytePerSec    = 0;
        request.mips_bw.busbwUsagePercentage = 0;
        request.mips_bw.set_latency     = TRUE;
        request.mips_bw.latency         = -1;
        retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            log_printf("HAP_power_set(HAP_power_set_mips_bw) failed (%d)\n", retval);
            return -1;
        }
    }
    return 0;
}

int halide_hexagon_remote_get_symbol_v2(handle_t module_ptr, const char* name, int nameLen,
                                        handle_t *sym_ptr) {
    *sym_ptr = reinterpret_cast<handle_t>(obj_dlsym(reinterpret_cast<elf_t*>(module_ptr), name));
    return *sym_ptr != 0 ? 0 : -1;
}

int halide_hexagon_remote_run(handle_t module_ptr, handle_t function,
                              const buffer *input_buffersPtrs, int input_buffersLen,
                              buffer *output_buffersPtrs, int output_buffersLen,
                              const buffer *input_scalarsPtrs, int input_scalarsLen) {

    // Get a pointer to the argv version of the pipeline.
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

    // Prior to running the pipeline, power HVX on (if it was not already on).
    int result = halide_hexagon_remote_power_hvx_on();
    if (result != 0) {
        return result;
    }

    // Call the pipeline and return the result.
    result = run_context.run(pipeline, args);

    // Power HVX off.
    halide_hexagon_remote_power_hvx_off();

    return result;
}

int halide_hexagon_remote_release_kernels(handle_t module_ptr, int codeLen) {
    obj_dlclose(reinterpret_cast<elf_t*>(module_ptr));
    return 0;
}

int halide_hexagon_remote_poll_profiler_state(int *func, int *threads) {
    *func = halide_profiler_get_state()->current_func;
    *threads = halide_profiler_get_state()->active_threads;
    return 0;
}

halide_profiler_state *halide_profiler_get_state() {
    static halide_profiler_state hvx_profiler_state;
    return &hvx_profiler_state;
}



}  // extern "C"
