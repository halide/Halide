#ifndef HALIDE_CODEGEN_D3D12_COMPUTE_DEV_H
#define HALIDE_CODEGEN_D3D12_COMPUTE_DEV_H

/** \file
 * Defines the code-generator for producing D3D12-compatible HLSL kernel code
 */

namespace Halide {

struct Target;

namespace Internal {

struct CodeGen_GPU_Dev;

CodeGen_GPU_Dev *new_CodeGen_D3D12Compute_Dev(const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
