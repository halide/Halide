#ifndef HALIDE_IR_ARM_OPTIMIZE_H
#define HALIDE_IR_ARM_OPTIMIZE_H

/** \file
 * Tools for optimizing IR for ARM.
 */

#include "CodeGen_LLVM.h"
#include "Expr.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Perform vector instruction selection, inserting VectorInstruction nodes. */
Stmt optimize_arm_instructions(const Stmt &stmt, const Target &target, const CodeGen_LLVM *codegen, const FuncValueBounds &fvb);

}  // namespace Internal
}  // namespace Halide

#endif
