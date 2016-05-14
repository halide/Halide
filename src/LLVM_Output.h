#ifndef HALIDE_LLVM_OUTPUTS_H
#define HALIDE_LLVM_OUTPUTS_H

/** \file
 *
 */

#include "Module.h"

namespace llvm {
class Module;
class TargetOptions;
class LLVMContext;
class raw_fd_ostream;
}

namespace Halide {

/** Generate an LLVM module. */
EXPORT std::unique_ptr<llvm::Module> compile_module_to_llvm_module(const Module &module, llvm::LLVMContext &context);

/** Construct an llvm output stream for writing to files. */
std::unique_ptr<llvm::raw_fd_ostream> make_raw_fd_ostream(const std::string &filename);

/** Compile an LLVM module to native targets (objects, native assembly). */
// @{
EXPORT void compile_llvm_module_to_object(llvm::Module &module, llvm::raw_fd_ostream& out);
EXPORT void compile_llvm_module_to_assembly(llvm::Module &module, llvm::raw_fd_ostream& out);
// @}

/** Compile an LLVM module to LLVM targets (bitcode, LLVM assembly). */
// @{
EXPORT void compile_llvm_module_to_llvm_bitcode(llvm::Module &module, llvm::raw_fd_ostream& out);
EXPORT void compile_llvm_module_to_llvm_assembly(llvm::Module &module, llvm::raw_fd_ostream& out);
// @}

}

#endif
