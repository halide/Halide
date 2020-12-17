#ifndef HALIDE_CODEGEN_OPENCL_DEV_H
#define HALIDE_CODEGEN_OPENCL_DEV_H

/** \file
 * Defines the code-generator for producing OpenCL C kernel code
 */

namespace Halide {

struct Target;

namespace Internal {

struct CodeGen_GPU_Dev;

CodeGen_GPU_Dev *new_CodeGen_OpenCL_Dev(const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
