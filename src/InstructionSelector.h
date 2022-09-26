#ifndef HALIDE_INSTRUCTION_SELECTOR_H
#define HALIDE_INSTRUCTION_SELECTOR_H

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
 *  div and mod, and splits VectorReduce nodes via
 *  CodeGen_LLVM::split_vector_reduce().
 */
class InstructionSelector : public IRGraphMutator {
protected:
    const Target &target;
    const CodeGen_LLVM *codegen;

    using IRGraphMutator::visit;
    Expr visit(const Div *) override;
    Expr visit(const Mod *) override;
    Expr visit(const VectorReduce *) override;

public:
    InstructionSelector(const Target &target, const CodeGen_LLVM *codegen);
};

}  // namespace Internal
}  // namespace Halide

#endif
