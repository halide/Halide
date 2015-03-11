#ifndef HALIDE_OUTPUTS_H
#define HALIDE_OUTPUTS_H

/** \file
 *
 * Provides output functions to enable writing out various build
 * objects from Halide Module objects.
 */

#include "Module.h"

namespace Halide {

/** Compile a halide Module to a native target (object file, native
 * assembly). The function that compiles both is more efficient
 * because it re-uses internal results. The default filename is the
 * name of the module with the default extension for the target type
 * (.o for objects, .s for assembly). */
// @{
EXPORT void compile_halide_module_to_object(const Module &module, std::string filename = "");
EXPORT void compile_halide_module_to_assembly(const Module &module, std::string filename = "");
EXPORT void compile_halide_module_to_native(const Module &module,
                                            std::string object_filename = "",
                                            std::string assembly_filename = "");
// @}

/** Compile a halide Module to an LLVM target (bitcode file, llvm
 * assembly). The function that compiles both is more efficient
 * because it re-uses internal results. The default filename is the
 * name of the module with the default extension for the target type
 * (.bc for bitcode, .ll for llvm assembly). */
// @{
EXPORT void compile_halide_module_to_bitcode(const Module &module, std::string filename = "");
EXPORT void compile_halide_module_to_llvm_assembly(const Module &module, std::string filename = "");
EXPORT void compile_halide_module_to_llvm(const Module &module,
                                          std::string bitcode_filename = "",
                                          std::string llvm_assembly_filename = "");
// @}

/** Output the module to C header/source code. The default filename is
 * the name of the module with the appropriate extension (.h/.c). */
// @{
EXPORT void compile_halide_module_to_c_header(const Module &module, std::string filename = "");
EXPORT void compile_halide_module_to_c_source(const Module &module, std::string filename = "");
EXPORT void compile_halide_module_to_c(const Module &module,
                                       std::string h_filename = "",
                                       std::string c_filename = "");
// @}

/** Output the module to HTML. The default filename is the name of the
 * module with the extension .html. */
EXPORT void compile_halide_module_to_html(const Module &module, std::string filename = "");

/** Output the module to a text statement file. The default filename
 * is the name of the module with the extension .stmt. */
EXPORT void compile_halide_module_to_text(const Module &module, std::string filename = "");

}

#endif
