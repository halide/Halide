#ifndef HALIDE_CODEGEN_HEXAGON_H
#define HALIDE_CODEGEN_HEXAGON_H

/** \file
 * Defines the code-generator for producing Hexagon machine code
 */

namespace llvm {

class LLVMContext;

}

namespace Halide {

struct Target;

namespace Internal {

class CodeGen_Posix;

CodeGen_Posix *new_CodeGen_Hexagon(const Target &target, llvm::LLVMContext &context);

}  // namespace Internal
}  // namespace Halide

#endif
