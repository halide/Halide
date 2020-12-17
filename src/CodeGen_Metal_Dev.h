#ifndef HALIDE_CODEGEN_METAL_DEV_H
#define HALIDE_CODEGEN_METAL_DEV_H

/** \file
 * Defines the code-generator for producing Apple Metal shading language kernel code
 */

namespace Halide {

struct Target;

namespace Internal {

struct CodeGen_GPU_Dev;

CodeGen_GPU_Dev *new_CodeGen_Metal_Dev(const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
