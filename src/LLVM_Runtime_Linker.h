#ifndef HALIDE_LLVM_RUNTIME_LINKER_H
#define HALIDE_LLVM_RUNTIME_LINKER_H

/** \file
 * Support for linking LLVM modules that comprise the runtime.
 */

#include "Target.h"

namespace llvm {
class Module;
class LLVMContext;
}

namespace Halide {

namespace Internal {

/** Create an llvm module containing the support code for a given target. */
llvm::Module *get_initial_module_for_target(Target, llvm::LLVMContext *, bool for_shared_jit_runtime = false, bool just_gpu = false);

/** Create an llvm module containing the support code for ptx device. */
llvm::Module *get_initial_module_for_ptx_device(Target, llvm::LLVMContext *c);

/** Create an llvm module containing the support code for renderscript. */
llvm::Module *get_initial_module_for_renderscript_device(Target target, llvm::LLVMContext *c);

}

}

#endif
