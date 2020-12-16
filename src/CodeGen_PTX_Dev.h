#ifndef HALIDE_CODEGEN_PTX_DEV_H
#define HALIDE_CODEGEN_PTX_DEV_H

/** \file
 * Defines the code-generator for producing CUDA host code
 */

namespace Halide {

struct Target;

namespace Internal {

struct CodeGen_GPU_Dev;

CodeGen_GPU_Dev *new_CodeGen_PTX_Dev(const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
