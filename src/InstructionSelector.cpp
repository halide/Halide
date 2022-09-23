#include "InstructionSelector.h"

#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

InstructionSelector::InstructionSelector(const Target &t, const CodeGen_LLVM *c, const FuncValueBounds &fvb)
    : target(t), codegen(c), func_value_bounds(fvb) {
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

Expr InstructionSelector::visit(const Let *op) {
    if (op->type.is_vector() && op->type.is_int_or_uint()) {
        // Query bounds and insert into scope.
        // TODO: should we always query here?
        Interval i = bounds_of_expr_in_scope(op->value, scope, func_value_bounds, false);
        ScopedBinding<Interval>(scope, op->name, i);
        return IRGraphMutator::visit(op);
    }

    return IRGraphMutator::visit(op);
}

Interval InstructionSelector::cached_get_interval(const Expr &expr) {
    const auto [iter, success] = cache.insert({expr, Interval::everything()});

    if (success) {
        // If we did insert, then actually store a real interval.
        // TODO: do we only want to store constant bounds? would be cheaper than using can_prove.
        iter->second = bounds_of_expr_in_scope(expr, scope, func_value_bounds, false);
    }

    return iter->second;
}

}  // namespace Internal
}  // namespace Halide
