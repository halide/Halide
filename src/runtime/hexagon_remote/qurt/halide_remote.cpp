#include "HalideRuntime.h"
#include "HalideRuntimeHexagonHost.h"
#include "halide_hexagon_remote.h"

#include <dlfcn.h>
#include <qurt.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "HAP_farf.h"
#include "HAP_power.h"
}

#include "known_symbols.h"
#include "log.h"

// const int stack_alignment = 128;
// const int stack_size = 1024 * 1024;

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;
typedef halide_hexagon_remote_scalar_t scalar_t;

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

__attribute__((weak)) void *dlopenbuf(const char *filename, const char *data, int size, int perms);

void *halide_get_symbol(const char *name) {
    // Try dlsym first. We need to try both RTLD_SELF and
    // RTLD_DEFAULT. Sometimes, RTLD_SELF finds a symbol when
    // RTLD_DEFAULT does not. This is surprising, I *think* RLTD_SELF
    // should search a subset of the symbols searched by
    // RTLD_DEFAULT...
    void *def = dlsym(RTLD_SELF, name);
    if (def) {
        return def;
    }
    def = dlsym(RTLD_DEFAULT, name);
    if (def) {
        return def;
    }

    // dlsym has some very unpredictable behavior that makes
    // it randomly unable to find symbols. To mitigate this, check our known symbols mapping.
    return get_known_symbol(name);
}

void *halide_load_library(const char *name) {
    return dlopen(name, RTLD_LAZY);
}

void *halide_get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

int halide_hexagon_remote_load_library(const char *soname, int sonameLen,
                                       const unsigned char *code, int codeLen,
                                       handle_t *module_ptr) {
    if (!dlopenbuf) {
        log_printf("dlopenbuf not available.");
        return -1;
    }
    void *lib = NULL;
    // We need to use RTLD_NOW, the libraries we build for Hexagon
    // offloading do not support lazy binding.
    lib = dlopenbuf(soname, (const char *)code, codeLen, RTLD_GLOBAL | RTLD_NOW);
    if (!lib) {
        log_printf("dlopenbuf failed: %s\n", dlerror());
        return -1;
    }

    *module_ptr = reinterpret_cast<handle_t>(lib);

    return 0;
}

volatile int power_ref_count = 0;

int halide_hexagon_remote_power_hvx_on() {
    if (power_ref_count == 0) {
        HAP_power_request_t request;
        request.type = HAP_power_set_HVX;
        request.hvx.power_up = TRUE;
        int result = HAP_power_set(NULL, &request);
        if (0 != result) {
            log_printf("HAP_power_set(HAP_power_set_HVX) failed (%d)\n", result);
            return -1;
        }
    }
    power_ref_count++;

    return 0;
}

int halide_hexagon_remote_power_hvx_off() {
    power_ref_count--;
    if (power_ref_count == 0) {
        HAP_power_request_t request;
        request.type = HAP_power_set_HVX;
        request.hvx.power_up = FALSE;
        int result = HAP_power_set(NULL, &request);
        if (0 != result) {
            log_printf("HAP_power_set(HAP_power_set_HVX) failed (%d)\n", result);
            return -1;
        }
    }
    return 0;
}

int halide_hexagon_remote_set_performance(
    int set_mips,
    unsigned int mipsPerThread,
    unsigned int mipsTotal,
    int set_bus_bw,
    unsigned int bwMegabytesPerSec,
    unsigned int busbwUsagePercentage,
    int set_latency,
    int latency) {

    HAP_power_request_t request;

    request.type = HAP_power_set_apptype;
    request.apptype = HAP_POWER_COMPUTE_CLIENT_CLASS;
    int retval = HAP_power_set(NULL, &request);
    if (0 != retval) {
        log_printf("HAP_power_set(HAP_power_set_apptype) failed (%d)\n", retval);
        return -1;
    }

    request.type = HAP_power_set_mips_bw;
    request.mips_bw.set_mips = set_mips;
    request.mips_bw.mipsPerThread = mipsPerThread;
    request.mips_bw.mipsTotal = mipsTotal;
    request.mips_bw.set_bus_bw = set_bus_bw;
    request.mips_bw.bwBytePerSec = ((uint64_t)bwMegabytesPerSec) << 20;
    request.mips_bw.busbwUsagePercentage = busbwUsagePercentage;
    request.mips_bw.set_latency = set_latency;
    request.mips_bw.latency = latency;
    retval = HAP_power_set(NULL, &request);
    if (0 != retval) {
        log_printf("HAP_power_set(HAP_power_set_mips_bw) failed (%d)\n", retval);
        return -1;
    }
    return 0;
}

HAP_dcvs_voltage_corner_t halide_power_mode_to_voltage_corner(int mode) {
    switch (mode) {
    case halide_hexagon_power_low:
        return HAP_DCVS_VCORNER_SVS;
    case halide_hexagon_power_nominal:
        return HAP_DCVS_VCORNER_NOM;
    case halide_hexagon_power_turbo:
        return HAP_DCVS_VCORNER_TURBO;
    case halide_hexagon_power_default:
        return HAP_DCVS_VCORNER_DISABLE;
    case halide_hexagon_power_low_plus:
        return HAP_DCVS_VCORNER_SVSPLUS;
    case halide_hexagon_power_low_2:
        return HAP_DCVS_VCORNER_SVS2;
    case halide_hexagon_power_nominal_plus:
        return HAP_DCVS_VCORNER_NOMPLUS;
    default:
        return HAP_DCVS_VCORNER_DISABLE;
    }
}

int halide_hexagon_remote_set_performance_mode(int mode) {
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
    HAP_power_request_t request;

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

    set_mips = TRUE;
    set_bus_bw = TRUE;
    set_latency = TRUE;
    switch (mode) {
    case halide_hexagon_power_low:
        mipsPerThread = max_mips / 4;
        bwBytePerSec = max_bus_bw / 2;
        busbwUsagePercentage = 25;
        latency = 1000;
        break;
    case halide_hexagon_power_nominal:
        mipsPerThread = (3 * max_mips) / 8;
        bwBytePerSec = max_bus_bw;
        busbwUsagePercentage = 50;
        latency = 100;
        break;
    case halide_hexagon_power_turbo:
        mipsPerThread = max_mips;
        bwBytePerSec = max_bus_bw * 4;
        busbwUsagePercentage = 100;
        latency = 10;
        break;
    case halide_hexagon_power_default:
    default:
        // These settings should reset the performance requested to
        // default.
        mipsPerThread = 0;
        bwBytePerSec = 0;
        busbwUsagePercentage = 0;
        latency = -1;
        break;
    }
    mipsTotal = mipsPerThread * 2;

    bwMegabytesPerSec = bwBytePerSec >> 20;

    memset(&request, 0, sizeof(HAP_power_request_t));
    request.type = HAP_power_set_apptype;
    request.apptype = HAP_POWER_COMPUTE_CLIENT_CLASS;
    retval = HAP_power_set(NULL, &request);
    if (0 != retval) {
        log_printf("HAP_power_set(HAP_power_set_apptype) failed (%d)\n", retval);
        return -1;
    }

    memset(&request, 0, sizeof(HAP_power_request_t));
    request.type = HAP_power_set_DCVS_v2;
    request.dcvs_v2.dcvs_enable = TRUE;
    request.dcvs_v2.dcvs_option = HAP_DCVS_V2_POWER_SAVER_MODE;
    request.dcvs_v2.set_dcvs_params = TRUE;
    request.dcvs_v2.dcvs_params.min_corner = HAP_DCVS_VCORNER_DISABLE;
    request.dcvs_v2.dcvs_params.max_corner = HAP_DCVS_VCORNER_DISABLE;
    request.dcvs_v2.dcvs_params.target_corner = halide_power_mode_to_voltage_corner(mode);
    request.dcvs_v2.set_latency = set_latency;
    request.dcvs_v2.latency = latency;
    retval = HAP_power_set(NULL, &request);
    if (0 == retval) {
        return 0;
    } else {
        return halide_hexagon_remote_set_performance(set_mips,
                                                     mipsPerThread,
                                                     mipsTotal,
                                                     set_bus_bw,
                                                     bwMegabytesPerSec,
                                                     busbwUsagePercentage,
                                                     set_latency,
                                                     latency);
    }
}

int halide_hexagon_remote_get_symbol_v4(handle_t module_ptr, const char *name, int nameLen, handle_t *sym_ptr) {
    *sym_ptr = reinterpret_cast<handle_t>(dlsym(reinterpret_cast<void *>(module_ptr), name));
    return *sym_ptr != 0 ? 0 : -1;
}

// Thread priority for QURT threads
// Negative: use the current default (don't explicitly reset it)
// Positive: the priority needs to be set once the shared runtime is loaded
int saved_thread_priority = -1;

int halide_hexagon_remote_set_thread_priority(int priority) {
    // Just save requested priority for now.  The priority can't actually
    // be set in qurt_thread_pool until the shared runtime has been loaded.
    saved_thread_priority = priority;
    return 0;
}

int halide_hexagon_runtime_set_thread_priority(int priority) {
    if (priority < 0) {
        return 0;
    }

    // Find the halide_set_default_thread_priority function in the shared runtime,
    // which we loaded with RTLD_GLOBAL.
    void (*set_priority)(int) = (void (*)(int))halide_get_symbol("halide_set_default_thread_priority");

    if (set_priority) {
        set_priority(priority);
    } else {
        // This code being run is old, doesn't have set priority feature, do nothing.
    }

    return 0;
}

int halide_hexagon_remote_run_v2(handle_t module_ptr, handle_t function,
                                 const buffer *input_buffersPtrs, int input_buffersLen,
                                 buffer *output_buffersPtrs, int output_buffersLen,
                                 const scalar_t *scalars, int scalarsLen) {
    // Get a pointer to the argv version of the pipeline.
    typedef int (*pipeline_argv_t)(void **);
    pipeline_argv_t pipeline = reinterpret_cast<pipeline_argv_t>(function);

    // Construct a list of arguments.
    struct hexagon_device_pointer {
        uint64_t dev;
        uint8_t *host;
    };

    void **args = NULL;
    hexagon_device_pointer *buffers = NULL;

    size_t args_size = (input_buffersLen + scalarsLen + output_buffersLen) * sizeof(void *);
    size_t buffers_size = (input_buffersLen + output_buffersLen) * sizeof(hexagon_device_pointer);

    // Threshold to allocate on heap vs stack.
    const size_t heap_allocation_threshold = 1024;
    bool allocated_on_heap = (args_size + buffers_size) > heap_allocation_threshold;

    if (allocated_on_heap) {
        args = (void **)malloc(args_size);
        buffers = (hexagon_device_pointer *)malloc(buffers_size);
    } else {
        args = (void **)__builtin_alloca(args_size);
        buffers = (hexagon_device_pointer *)__builtin_alloca(buffers_size);
    }
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
    for (int i = 0; i < scalarsLen; i++, next_arg++) {
        *next_arg = const_cast<scalar_t *>(&scalars[i]);
    }

    // Prior to running the pipeline, power HVX on (if it was not already on).
    int result = halide_hexagon_remote_power_hvx_on();
    if (result != 0) {
        if (allocated_on_heap) {
            free(buffers);
            free(args);
        }
        return result;
    }

    // Call the pipeline and return the result.
    result = pipeline(args);

    // Power HVX off.
    halide_hexagon_remote_power_hvx_off();

    if (allocated_on_heap) {
        free(buffers);
        free(args);
    }

    return result;
}

int halide_hexagon_remote_release_library(handle_t module_ptr) {
    dlclose(reinterpret_cast<void *>(module_ptr));
    return 0;
}

int halide_hexagon_remote_poll_profiler_state(int *func, int *threads) {
    // Increase the current thread priority to match working threads priorities,
    // so profiler can access the remote state without extra latency.
    qurt_thread_t current_thread_id = qurt_thread_get_id();
    qurt_thread_set_priority(current_thread_id, 100);

    *func = halide_profiler_get_state()->current_func;
    *threads = halide_profiler_get_state()->active_threads;
    return 0;
}
int halide_hexagon_remote_profiler_set_current_func(int current_func) {
    halide_profiler_get_state()->current_func = current_func;
    return 0;
}
halide_profiler_state *halide_profiler_get_state() {
    static halide_profiler_state hvx_profiler_state;
    return &hvx_profiler_state;
}

}  // extern "C"
