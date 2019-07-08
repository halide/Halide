#ifndef HL_PYTORCH_CUDA_HELPERS_H
#define HL_PYTORCH_CUDA_HELPERS_H

#ifdef HL_PT_CUDA
#include <HalideRuntimeCuda.h>
#include <cuda.h>

namespace Halide {
namespace Pytorch {

typedef struct UserContext {
  UserContext(int id, CUcontext *ctx, cudaStream_t* stream) :
    device_id(id), cuda_context(ctx), stream(stream) {};

  int device_id;
  CUcontext *cuda_context;
  cudaStream_t *stream;
} UserContext;

} // namespace Pytorch
} // namespace Halide

// Replace Halide weakly-linked cuda handles
extern "C" {

WEAK int halide_cuda_acquire_context(void *user_context, CUcontext *ctx, bool create = true) {
  if(user_context != NULL) {
    Halide::Pytorch::UserContext *user_ctx = (Halide::Pytorch::UserContext*) user_context;
    // std::cerr << "PyWrap get ctx " << *user_ctx->cuda_context << "\n";
    *ctx = *user_ctx->cuda_context;
  } else {
    // std::cerr << "no user context\n";
    *ctx = NULL;
  }
  return 0;
}

WEAK int halide_cuda_get_stream(void *user_context, CUcontext ctx, CUstream *stream) {
  if(user_context != NULL) {
    Halide::Pytorch::UserContext *user_ctx = (Halide::Pytorch::UserContext*) user_context;
    // std::cerr << "PyWrap's get stream " <<  *user_ctx->stream << "\n";
    *stream = *user_ctx->stream;
  } else {
    // printf("no user context, using default stream \n");
    *stream = 0;
  }
  return 0;
}

WEAK int halide_get_gpu_device(void *user_context) {
  if(user_context != NULL) {
    Halide::Pytorch::UserContext *user_ctx = (Halide::Pytorch::UserContext*) user_context;
    // std::cerr << "PyWrap's get gpu device " <<  user_ctx->device_id << "\n";
    return user_ctx->device_id;
  } else {
    // std::cerr << "no user context, using default device \n";
    return 0;
  }
}
}  // extern "C"

#endif  // HL_PT_CUDA

#endif /* end of include guard: HL_PYTORCH_CUDA_HELPERS_H */
