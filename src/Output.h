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

    /** The name of the emitted C header file. Empty if no C header file
     * output is desired. */
    std::string c_header_name;

    /** The name of the emitted C source file. Empty if no C source file
     * output is desired. */
    std::string c_source_name;

    /** The name of the emitted stmt file. Empty if no stmt file
     * output is desired. */
    std::string stmt_name;

    /** The name of the emitted stmt.html file. Empty if no stmt.html file
     * output is desired. */
    std::string stmt_html_name;

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

    /** Make a new Outputs struct that emits everything this one does
     * and also a C header file with the given name. */
    Outputs c_header(const std::string &c_header_name) {
        Outputs updated = *this;
        updated.c_header_name = c_header_name;
        return updated;
    }

    /** Make a new Outputs struct that emits everything this one does
     * and also a C source file with the given name. */
    Outputs c_source(const std::string &c_source_name) {
        Outputs updated = *this;
        updated.c_source_name = c_source_name;
        return updated;
    }

    /** Make a new Outputs struct that emits everything this one does
     * and also a stmt file with the given name. */
    Outputs stmt(const std::string &stmt_name) {
        Outputs updated = *this;
        updated.stmt_name = stmt_name;
        return updated;
    }

    /** Make a new Outputs struct that emits everything this one does
     * and also a stmt.html file with the given name. */
    Outputs stmt_html(const std::string &stmt_html_name) {
        Outputs updated = *this;
        updated.stmt_html_name = stmt_html_name;
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

/** Create an object file containing the Halide runtime for a given
 * target. For use with Target::NoRuntime. */
EXPORT void compile_standalone_runtime(std::string object_filename, Target t);

}

#endif
