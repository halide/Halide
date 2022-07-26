#ifndef HALIDE_IR_X86_OPTIMIZE_H
#define HALIDE_IR_X86_OPTIMIZE_H

/** \file
 * Tools for optimizing IR for x86.
 */

#include "CodeGen_LLVM.h"
#include "Expr.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Perform vector instruction selection, inserting VectorIntrinsic nodes. */
Stmt optimize_x86_instructions(Stmt stmt, const Target &target, const CodeGen_LLVM *codegen);

}  // namespace Internal
}  // namespace Halide

#endif
