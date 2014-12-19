#ifndef HALIDE_OUTPUTS_H
#define HALIDE_OUTPUTS_H

/** \file
 *
 * Provides output functions to enable writing out various build
 * objects from Halide Module objects.
 */

#include "Module.h"

namespace Halide {

EXPORT void output_object(const Module &module, const std::string &filename);
EXPORT void output_assembly(const Module &module, const std::string &filename);
EXPORT void output_native(const Module &module,
                          const std::string &object_filename,
                          const std::string &assembly_filename);

EXPORT void output_bitcode(const Module &module, const std::string &filename);
EXPORT void output_llvm_assembly(const Module &module, const std::string &filename);
EXPORT void output_llvm(const Module &module,
                        const std::string &bitcode_filename,
                        const std::string &llvm_assembly_filename);

EXPORT void output_stmt_html(const Module &module, const std::string &filename);
EXPORT void output_stmt_text(const Module &module, const std::string &filename);

EXPORT void output_c_header(const Module &module, const std::string &filename);
EXPORT void output_c_source(const Module &module, const std::string &filename);
EXPORT void output_c(const Module &module,
                     const std::string &h_filename,
                     const std::string &c_filename);

}

#endif
