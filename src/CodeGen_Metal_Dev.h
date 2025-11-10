#ifndef HALIDE_CODEGEN_METAL_DEV_H
#define HALIDE_CODEGEN_METAL_DEV_H

/** \file
 * Defines the code-generator for producing Apple Metal shading language kernel code
 */

#include <memory>
#include <string>

namespace Halide {

struct Target;

/** Set the Metal compiler and linker commands to use for generating
 * precompiled Metal shaders (embedded metallibs instead of source code).
 * If both are set to non-empty strings, the Metal code generator will invoke
 * these tools to precompile shaders instead of embedding source code.
 * The compiler should typically be set to something like "xcrun -sdk macosx metal"
 * and the linker to "xcrun -sdk macosx metallib".
 * @param compiler_path The path/command for the Metal compiler
 * @param linker_path The path/command for the Metal linker
 */
void set_metal_compiler_and_linker(const std::string &compiler_path,
                                   const std::string &linker_path);

namespace Internal {

struct CodeGen_GPU_Dev;

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_Metal_Dev(const Target &target);

/** Get the Metal compiler command that was set via set_metal_compiler_and_linker().
 * Returns empty string if not set. */
std::string get_metal_compiler();

/** Get the Metal linker command that was set via set_metal_compiler_and_linker().
 * Returns empty string if not set. */
std::string get_metal_linker();

}  // namespace Internal
}  // namespace Halide

#endif
