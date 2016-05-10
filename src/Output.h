#ifndef HALIDE_OUTPUTS_H
#define HALIDE_OUTPUTS_H

/** \file
 *
 * Provides output functions to enable writing out various build
 * objects from Halide Module objects.
 */

#include <string>

#include "Module.h"

namespace Halide {

/** A struct specifying a collection of outputs. Used as an argument
 * to Pipeline::compile_to and Func::compile_to */
struct Outputs {
    /** The name of the emitted object file. Empty if no object file
     * output is desired. */
    std::string object_name;

    /** The name of the emitted text assembly file. Empty if no
     * assembly file output is desired. */
    std::string assembly_name;

    /** The name of the emitted llvm bitcode. Empty if no llvm bitcode
     * output is desired. */
    std::string bitcode_name;

    /** The name of the emitted llvm assembly. Empty if no llvm assembly
     * output is desired. */
    std::string llvm_assembly_name;

    /** Make a new Outputs struct that emits everything this one does
     * and also an object file with the given name. */
    Outputs object(const std::string &object_name) {
        Outputs updated = *this;
        updated.object_name = object_name;
        return updated;
    }

    /** Make a new Outputs struct that emits everything this one does
     * and also an assembly file with the given name. */
    Outputs assembly(const std::string &assembly_name) {
        Outputs updated = *this;
        updated.assembly_name = assembly_name;
        return updated;
    }

    /** Make a new Outputs struct that emits everything this one does
     * and also an llvm bitcode file with the given name. */
    Outputs bitcode(const std::string &bitcode_name) {
        Outputs updated = *this;
        updated.bitcode_name = bitcode_name;
        return updated;
    }

    /** Make a new Outputs struct that emits everything this one does
     * and also an llvm assembly file with the given name. */
    Outputs llvm_assembly(const std::string &llvm_assembly_name) {
        Outputs updated = *this;
        updated.llvm_assembly_name = llvm_assembly_name;
        return updated;
    }
};

/** Compile a halide Module to a native target (object file, native
 * assembly) or an LLVM Target (bitcode file, llvm assembly), depending on 
 * the target setting of the Module. The function that compiles both is more efficient
 * because it re-uses internal results. The default filename is the
 * name of the module with the default extension for the target type
 * (.o for objects, .s for assembly). */
EXPORT void compile_module_to_outputs(const Module &module, const Outputs &output_files);

/** Compile a halide Module to a native target (object file, native
 * assembly). The function that compiles both is more efficient
 * because it re-uses internal results. The default filename is the
 * name of the module with the default extension for the target type
 * (.o for objects, .s for assembly). */
// @{
EXPORT void compile_module_to_object(const Module &module, std::string filename = "");
EXPORT void compile_module_to_assembly(const Module &module, std::string filename = "");
EXPORT void compile_module_to_native(const Module &module,
                                     std::string object_filename = "",
                                     std::string assembly_filename = "");
// @}

/** Compile a halide Module to an LLVM target (bitcode file, llvm
 * assembly). The function that compiles both is more efficient
 * because it re-uses internal results. The default filename is the
 * name of the module with the default extension for the target type
 * (.bc for bitcode, .ll for llvm assembly). */
// @{
EXPORT void compile_module_to_llvm_bitcode(const Module &module, std::string filename = "");
EXPORT void compile_module_to_llvm_assembly(const Module &module, std::string filename = "");
EXPORT void compile_module_to_llvm(const Module &module,
                                   std::string bitcode_filename = "",
                                   std::string llvm_assembly_filename = "");
// @}

/** Output the module to C header/source code. The default filename is
 * the name of the module with the appropriate extension (.h/.c). */
// @{
EXPORT void compile_module_to_c_header(const Module &module, std::string filename = "");
EXPORT void compile_module_to_c_source(const Module &module, std::string filename = "");
EXPORT void compile_module_to_c(const Module &module,
                                std::string h_filename = "",
                                std::string c_filename = "");
// @}

/** Output the module to HTML. The default filename is the name of the
 * module with the extension .html. */
EXPORT void compile_module_to_html(const Module &module, std::string filename = "");

/** Output the module to a text statement file. The default filename
 * is the name of the module with the extension .stmt. */
EXPORT void compile_module_to_text(const Module &module, std::string filename = "");

/** Create an object file containing the Halide runtime for a given
 * target. For use with Target::NoRuntime. */
EXPORT void compile_standalone_runtime(std::string object_filename, Target t);

}

#endif
