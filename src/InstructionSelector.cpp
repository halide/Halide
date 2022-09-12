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

Interval InstructionSelector::cached_get_interval(const Expr &expr) {
    const auto [iter, success] = cache.insert({expr, Interval::everything()});

    if (success) {
        // If we did insert, then actually store a real interval.
        // TODO: do we only want to store constant bounds? would be cheaper than using can_prove.
        iter->second = bounds_of_expr_in_scope(expr, scope, func_value_bounds, false);
    }

    return iter->second;
}

bool InstructionSelector::is_upper_bounded(const Expr &expr, const int64_t bound) {
    internal_assert(expr.type().element_of().can_represent(bound))
        << "Type of expr cannot represent upper bound:\n " << expr << "\n " << bound << "\n";

    Expr e = make_const(expr.type().element_of(), bound);
    const Interval i = cached_get_interval(expr);
    // TODO: see above - we could get rid of can_prove if we use constant bounds queries instead.
    return can_prove(i.max <= e);
}

bool InstructionSelector::is_upper_bounded(const Expr &expr, const uint64_t bound) {
    internal_assert(expr.type().element_of().can_represent(bound))
        << "Type of expr cannot represent upper bound:\n " << expr << "\n " << bound << "\n";

    Expr e = make_const(expr.type().element_of(), bound);
    const Interval i = cached_get_interval(expr);
    // TODO: see above - we could get rid of can_prove if we use constant bounds queries instead.
    return can_prove(i.max <= e);
}

bool InstructionSelector::is_lower_bounded(const Expr &expr, const int64_t bound) {
    internal_assert(expr.type().element_of().can_represent(bound))
        << "Type of expr cannot represent lower bound:\n " << expr << "\n " << bound << "\n";

    Expr e = make_const(expr.type().element_of(), bound);
    const Interval i = cached_get_interval(expr);
    // TODO: see above - we could get rid of can_prove if we use constant bounds queries instead.
    return can_prove(i.min >= e);
}

bool InstructionSelector::is_lower_bounded(const Expr &expr, const uint64_t bound) {
    internal_assert(expr.type().element_of().can_represent(bound))
        << "Type of expr cannot represent lower bound:\n " << expr << "\n " << bound << "\n";

    Expr e = make_const(expr.type().element_of(), bound);
    const Interval i = cached_get_interval(expr);
    // TODO: see above - we could get rid of can_prove if we use constant bounds queries instead.
    return can_prove(i.min >= e);
}

}  // namespace Internal
}  // namespace Halide
