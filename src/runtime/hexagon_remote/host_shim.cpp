#include <memory.h>
#include <android/log.h>

#include "bin/src/halide_hexagon_remote.h"

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
            __android_log_print(ANDROID_LOG_ERROR, "halide", "Scalar argument %d is larger than %d bytes (%d bytes)",
                                i, sizeof(scalar_t), scalar_size);
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

}  // extern "C"
