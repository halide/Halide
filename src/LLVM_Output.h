#ifndef HALIDE_LLVM_OUTPUTS_H
#define HALIDE_LLVM_OUTPUTS_H

/** \file
 *
 */

#include "Module.h"

namespace llvm {
class Module;
class TargetOptions;
}

namespace Halide {

EXPORT void get_target_options(const llvm::Module *module, llvm::TargetOptions &options, std::string &mcpu, std::string &mattrs);

EXPORT llvm::Module *output_llvm_module(const Module &module);

EXPORT void output_object(llvm::Module *module, const std::string &filename);
EXPORT void output_assembly(llvm::Module *module, const std::string &filename);
EXPORT void output_native(llvm::Module *module,
                          const std::string &object_filename,
                          const std::string &assembly_filename);

EXPORT void output_bitcode(llvm::Module *module, const std::string &filename);
EXPORT void output_llvm_assembly(llvm::Module *module, const std::string &filename);
EXPORT void output_llvm(llvm::Module *module,
                        const std::string &bitcode_filename,
                        const std::string &llvm_assembly_filename);

}

#endif
