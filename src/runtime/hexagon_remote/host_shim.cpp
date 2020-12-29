#include <android/log.h>
#include <memory.h>
#include <stdio.h>

#include "halide_hexagon_remote.h"

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;
typedef halide_hexagon_remote_scalar_t scalar_t;

extern "C" {

// In v2, we pass all scalars and small input buffers in a single buffer.
int halide_hexagon_remote_run(handle_t module_ptr, handle_t function,
                              buffer *input_buffersPtrs, int input_buffersLen,
                              buffer *output_buffersPtrs, int output_buffersLen,
                              const buffer *input_scalarsPtrs, int input_scalarsLen) {
    // Pack all of the scalars into an array of scalar_t.
    scalar_t *scalars = (scalar_t *)__builtin_alloca(input_scalarsLen * sizeof(scalar_t));
    for (int i = 0; i < input_scalarsLen; i++) {
        int scalar_size = input_scalarsPtrs[i].dataLen;
        if (scalar_size > sizeof(scalar_t)) {
            __android_log_print(ANDROID_LOG_ERROR, "halide", "Scalar argument %d is larger than %lld bytes (%d bytes)",
                                i, (long long)sizeof(scalar_t), scalar_size);
            return -1;
        }
        memcpy(&scalars[i], input_scalarsPtrs[i].data, scalar_size);
    }

    // Call v2 with the adapted arguments.
    return halide_hexagon_remote_run_v2(module_ptr, function,
                                        input_buffersPtrs, input_buffersLen,
                                        output_buffersPtrs, output_buffersLen,
                                        scalars, input_scalarsLen);
}

// Before load_library, initialize_kernels did not take an soname parameter.
int halide_hexagon_remote_initialize_kernels_v3(const unsigned char *code, int codeLen, handle_t *module_ptr) {
    // We need a unique soname, or dlopenbuf will return a
    // previously opened library.
    static int unique_id = 0;
    char soname[256];
    sprintf(soname, "libhalide_kernels%04d.so", __sync_fetch_and_add(&unique_id, 1));

    return halide_hexagon_remote_load_library(soname, strlen(soname) + 1, code, codeLen, module_ptr);
}

// This is just a renaming.
int halide_hexagon_remote_release_kernels_v2(handle_t module_ptr) {
    return halide_hexagon_remote_release_library(module_ptr);
}

}  // extern "C"
