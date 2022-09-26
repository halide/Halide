#include "InstructionSelector.h"

#include "CodeGen_Internal.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

InstructionSelector::InstructionSelector(const Target &t, const CodeGen_LLVM *c)
    : target(t), codegen(c) {
}

Expr InstructionSelector::visit(const Div *op) {
    if (op->type.is_vector() && op->type.is_int_or_uint()) {
        // Lower division here in order to do pattern-matching on intrinsics.
        return mutate(lower_int_uint_div(op->a, op->b));
    }
    return IRGraphMutator::visit(op);
}

Expr InstructionSelector::visit(const Mod *op) {
    if (op->type.is_vector() && op->type.is_int_or_uint()) {
        // Lower mod here in order to do pattern-matching on intrinsics.
        return mutate(lower_int_uint_mod(op->a, op->b));
    }
    return IRGraphMutator::visit(op);
}

Expr InstructionSelector::visit(const VectorReduce *op) {
    return mutate(codegen->split_vector_reduce(op, Expr()));
}

}  // namespace Internal
}  // namespace Halide
