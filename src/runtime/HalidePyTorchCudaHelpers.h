#ifndef HL_PYTORCH_CUDA_HELPERS_H
#define HL_PYTORCH_CUDA_HELPERS_H

/** \file
 * Override Halide's CUDA hooks so that the Halide code called from PyTorch uses 
 * the correct GPU device and stream.
 */

#ifdef HL_PT_CUDA
#include "HalideRuntimeCuda.h"
#include "cuda.h"

namespace Halide {
namespace PyTorch {

typedef struct UserContext {
    UserContext(int id, CUcontext *ctx, cudaStream_t* stream) :
      device_id(id), cuda_context(ctx), stream(stream) {};

    int device_id;
    CUcontext *cuda_context;
    cudaStream_t *stream;
} UserContext;

} // namespace PyTorch
} // namespace Halide


// Replace Halide weakly-linked CUDA handles
extern "C" {

int halide_cuda_acquire_context(void *user_context, CUcontext *ctx, bool create = true) {
    if (user_context != NULL) {
        Halide::PyTorch::UserContext *user_ctx = (Halide::PyTorch::UserContext*) user_context;
        *ctx = *user_ctx->cuda_context;
    } else {
        *ctx = NULL;
    }
    return 0;
}


int halide_cuda_get_stream(void *user_context, CUcontext ctx, CUstream *stream) {
    if (user_context != NULL) {
        Halide::PyTorch::UserContext *user_ctx = (Halide::PyTorch::UserContext*) user_context;
        *stream = *user_ctx->stream;
    } else {
        *stream = 0;
    }
    return 0;
}


int halide_get_gpu_device(void *user_context) {
    if (user_context != NULL) {
        Halide::PyTorch::UserContext *user_ctx = (Halide::PyTorch::UserContext*) user_context;
        return user_ctx->device_id;
    } else {
        return 0;
    }
}


}  // extern "C"

#endif  // HL_PT_CUDA

#endif /* end of include guard: HL_PYTORCH_CUDA_HELPERS_H */
