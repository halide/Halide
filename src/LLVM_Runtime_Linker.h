#ifndef HALIDE_LLVM_RUNTIME_LINKER_H
#define HALIDE_LLVM_RUNTIME_LINKER_H

/** \file
 * Support for linking LLVM modules that comprise the runtime.
 */

#include <memory>
#include "Target.h"

namespace llvm {
class Module;
class LLVMContext;
class Triple;
}  // namespace llvm

namespace Halide {
namespace Internal {

/** Return the llvm::Triple that corresponds to the given Halide Target */
llvm::Triple get_triple_for_target(const Target &target);

/** Create an llvm module containing the support code for a given target. */
std::unique_ptr<llvm::Module> get_initial_module_for_target(Target, llvm::LLVMContext *, bool for_shared_jit_runtime = false, bool just_gpu = false);

/** Create an llvm module containing the support code for ptx device. */
std::unique_ptr<llvm::Module> get_initial_module_for_ptx_device(Target, llvm::LLVMContext *c);

/** Link a block of llvm bitcode into an llvm module. */
void add_bitcode_to_module(llvm::LLVMContext *context, llvm::Module &module,
                           const std::vector<uint8_t> &bitcode, const std::string &name);

}  // namespace Internal
}  // namespace Halide

#endif
