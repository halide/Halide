#ifndef HALIDE_CODEGEN_OPENGLCOMPUTE_DEV_H
#define HALIDE_CODEGEN_OPENGLCOMPUTE_DEV_H

/** \file
 * Defines the code-generator for producing GLSL kernel code for OpenGL Compute.
 */

namespace Halide {

struct Target;

namespace Internal {

struct CodeGen_GPU_Dev;

CodeGen_GPU_Dev *new_CodeGen_OpenGLCompute_Dev(const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
