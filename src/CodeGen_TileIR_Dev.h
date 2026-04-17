#ifndef HALIDE_CODEGEN_TILE_IR_DEV_H
#define HALIDE_CODEGEN_TILE_IR_DEV_H

/** \file
 * Defines the code-generator for producing NVIDIA Tile IR bytecode.
 */

#include <memory>

#include "Expr.h"

namespace Halide {

struct Target;

namespace Internal {

struct CodeGen_GPU_Dev;

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_TileIR_Dev(const Target &target);

/** Pattern-match VectorReduce-based matmul expressions in the Halide IR
 * and rewrite them into `tile_ir_mmaf` intrinsic calls that the Tile IR
 * backend can lower to `cuda_tile.mmaf`. Must run before
 * flatten_nested_ramps — that pass scrambles the shape info this matcher
 * uses to identify the MMA tile structure. */
Stmt lower_cuda_tile_mma(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
