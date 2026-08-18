#ifndef HALIDE_RUNTIME_CUDA_WMMA_H
#define HALIDE_RUNTIME_CUDA_WMMA_H

#include "runtime_internal.h"

#include "mini_cuda.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Cuda {

/** Finish off the tensor core instructions codegen left unfinished in a PTX
 * module. How the hardware spreads a fragment across the registers of a warp
 * isn't architecturally specified, so anything that depends on it is left as a
 * marker for this to fill in, once, having measured the layout by running a
 * probe kernel. See constants.h for what a marker looks like.
 *
 * Returns the module to load: the one passed in if it had no markers, a copy
 * of it for the caller to free if it did, or nullptr if something went wrong,
 * having reported why. */
WEAK const char *finish_wmma_markers(void *user_context, CUcontext ctx,
                                     const char *ptx_src);

}  // namespace Cuda
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif
