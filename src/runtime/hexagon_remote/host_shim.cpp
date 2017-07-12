#include <memory.h>

#include "bin/src/halide_hexagon_remote.h"
#include "pack_buffer.h"

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;

extern "C" {

// In v2, we pass all scalars and small input buffers in a single buffer.
int halide_hexagon_remote_run(handle_t module_ptr, handle_t function,
                              buffer *input_buffersPtrs, int input_buffersLen,
                              buffer *output_buffersPtrs, int output_buffersLen,
                              const buffer *input_scalarsPtrs, int input_scalarsLen) {
    const int buffer_alignment = 128;
    const int scalar_alignment = 4;

    int small_input_args_size = 0;

    // Add up the number and size of the small input buffers. Buffers
    // are considered small if they are less than
    // min_input_buffer_size bytes.
    const int min_input_buffer_size = 4096;
    for (int i = 0; i < input_buffersLen; i++) {
        if (input_buffersPtrs[i].dataLen < min_input_buffer_size) {
            small_input_args_size += packed_buffer_size(input_buffersPtrs[i].dataLen, buffer_alignment);
        }
    }

    // Add up the number and size of the scalars.
    for (int i = 0; i < input_scalarsLen; i++) {
        small_input_args_size += packed_buffer_size(input_scalarsPtrs[i].dataLen, scalar_alignment);
    }

    // Allocate a buffer to hold the packed small buffers and scalars.
    unsigned char *small_input_args = (unsigned char *)__builtin_alloca(small_input_args_size + buffer_alignment);
    small_input_args = (unsigned char *)(((uintptr_t)small_input_args + buffer_alignment - 1) & ~(buffer_alignment - 1));

    // Pack up the small buffers.
    unsigned char *write = small_input_args;
    for (int i = 0; i < input_buffersLen; i++) {
        if (input_buffersPtrs[i].dataLen < min_input_buffer_size) {
            write_buffer(write, input_buffersPtrs[i].data, input_buffersPtrs[i].dataLen, buffer_alignment);

            // Remove the buffer from the actual buffers list.
            input_buffersPtrs[i].data = NULL;
            input_buffersPtrs[i].dataLen = 0;
        }
    }

    // Pack up the scalars.
    for (int i = 0; i < input_scalarsLen; i++) {
        write_buffer(write, input_scalarsPtrs[i].data, input_scalarsPtrs[i].dataLen, scalar_alignment);
    }

    // Call v2 with the adapted arguments.
    return halide_hexagon_remote_run_v2(module_ptr, function,
                                        input_buffersPtrs, input_buffersLen,
                                        output_buffersPtrs, output_buffersLen,
                                        input_scalarsLen, small_input_args, small_input_args_size);
}

}  // extern "C"
