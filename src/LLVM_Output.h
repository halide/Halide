#ifndef HALIDE_LLVM_OUTPUTS_H
#define HALIDE_LLVM_OUTPUTS_H

/** \file
 *
 */

#include <string>
#include <vector>

#include "Module.h"
#include "Target.h"
#include "Util.h"

namespace llvm {
class Module;
class TargetOptions;
class LLVMContext;
class raw_fd_ostream;
class raw_pwrite_stream;
class raw_ostream;
}  // namespace llvm

namespace Halide {

namespace Internal {
typedef llvm::raw_pwrite_stream LLVMOStream;
}

/** Generate an LLVM module. */
std::unique_ptr<llvm::Module> compile_module_to_llvm_module(const Module &module, llvm::LLVMContext &context);

/** Construct an llvm output stream for writing to files. */
std::unique_ptr<llvm::raw_fd_ostream> make_raw_fd_ostream(const std::string &filename);

/** Compile an LLVM module to native targets (objects, native assembly). */
// @{
void compile_llvm_module_to_object(llvm::Module &module, Internal::LLVMOStream& out);
void compile_llvm_module_to_assembly(llvm::Module &module, Internal::LLVMOStream& out);
// @}

/** Compile an LLVM module to LLVM targets (bitcode, LLVM assembly). */
// @{
void compile_llvm_module_to_llvm_bitcode(llvm::Module &module, Internal::LLVMOStream& out);
void compile_llvm_module_to_llvm_assembly(llvm::Module &module, Internal::LLVMOStream& out);
// @}

/**
 * Concatenate the list of src_files into dst_file, using the appropriate
 * static library format for the given target (e.g., .a or .lib).
 * If deterministic is true, emit 0 for all GID/UID/timestamps, and 0644 for
 * all modes (equivalent to the ar -D option).
 */
void create_static_library(const std::vector<std::string> &src_files, const Target &target,
                           const std::string &dst_file, bool deterministic = true);
}  // namespace Halide

#endif
