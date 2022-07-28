#ifndef HALIDE_INSTR_SELECTOR_H
#define HALIDE_INSTR_SELECTOR_H

/** \file
 * Defines a base class for VectorInstruction selection.
 */

#include "CodeGen_LLVM.h"
#include "IR.h"
#include "IRMutator.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** A base class for vector instruction selection.
 *  The default implementation lowers int and uint
 *  division via `lower_int_uint_div` and splits
 *  VectorReduce nodes via CodeGen_LLVM::split_vector_reduce().
 */
class InstructionSelector : public IRGraphMutator {
protected:
    const Target &target;
    const CodeGen_LLVM *codegen;

    Expr visit(const Div *) override;
    Expr visit(const VectorReduce *) override;

public:
    InstructionSelector(const Target &target, const CodeGen_LLVM *codegen);
};

}  // namespace Internal
}  // namespace Halide

#endif
