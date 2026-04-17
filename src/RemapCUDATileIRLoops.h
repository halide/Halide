#ifndef HALIDE_REMAP_CUDA_TILE_IR_LOOPS_H
#define HALIDE_REMAP_CUDA_TILE_IR_LOOPS_H

/** \file
 * Defines the pass that remaps GPUThread loops with DeviceAPI::CUDATileIR
 * to Vectorized loops, so the vectorizer produces Ramp/Broadcast/vector
 * operations that map naturally to Tile IR.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Remap CUDATileIR GPU thread loops to vectorized loops. GPUBlock loops
 * are left unchanged. This should run after select_gpu_api but before
 * vectorize_loops. */
Stmt remap_cuda_tile_ir_loops(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
