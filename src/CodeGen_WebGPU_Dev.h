#ifndef HALIDE_CODEGEN_WEBGPU_DEV_H
#define HALIDE_CODEGEN_WEBGPU_DEV_H

/** \file
 * Defines the code-generator for producing WebGPU shader code (WGSL)
 */

#include <memory>

namespace Halide {

struct Target;

namespace Internal {

struct CodeGen_GPU_Dev;

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_WebGPU_Dev(const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
