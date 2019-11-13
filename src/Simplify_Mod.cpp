#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Mod *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    // We always combine bounds here, even if not requested, because
    // we can use them to simplify down to a constant if the bounds
    // are tight enough.
    ExprInfo mod_bounds;

    // Just use the bounds of the RHS
    if (no_overflow_int(op->type)) {
        mod_bounds.min_defined = mod_bounds.max_defined =
            (b_bounds.min_defined && b_bounds.max_defined &&
             (b_bounds.min > 0 || b_bounds.max < 0));
        mod_bounds.min = 0;
        mod_bounds.max = std::max(std::abs(b_bounds.min), std::abs(b_bounds.max)) - 1;
        mod_bounds.alignment = a_bounds.alignment % b_bounds.alignment;
        mod_bounds.trim_bounds_using_alignment();
        if (bounds) {
            *bounds = mod_bounds;
        }
    }

    if (may_simplify(op->type)) {
        if (a_bounds.min_defined && a_bounds.min >= 0 &&
            a_bounds.max_defined && b_bounds.min_defined && a_bounds.max < b_bounds.min) {
            return a;
        }

        if (mod_bounds.min_defined && mod_bounds.max_defined && mod_bounds.min == mod_bounds.max) {
            return make_const(op->type, mod_bounds.min);
        }

        int lanes = op->type.lanes();

        auto rewrite = IRMatcher::rewriter(IRMatcher::mod(a, b), op->type);

        if (rewrite(c0 % c1, fold(c0 % c1)) ||
            rewrite(IRMatcher::Indeterminate() % x, a) ||
            rewrite(x % IRMatcher::Indeterminate(), b) ||
            rewrite(IRMatcher::Overflow() % x, a) ||
            rewrite(x % IRMatcher::Overflow(), b) ||
            rewrite(0 % x, 0) ||
            rewrite(x % x, 0) ||
            (!op->type.is_float() &&
             (rewrite(x % 0, IRMatcher::Indeterminate()) ||
              rewrite(x % 1, 0)))) {
            return rewrite.result;
        }

        if (EVAL_IN_LAMBDA
            (rewrite(broadcast(x) % broadcast(y), broadcast(x % y, lanes)) ||
             (no_overflow_int(op->type) &&
              #ifdef EXCLUDE_INVALID_ORDERING_RULES
              (rewrite((x * c0) % c1, (x * fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               rewrite((x + c0) % c1, (x + fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               #endif
               rewrite((x * c0) % c1, (x % fold(c1/c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
               rewrite((x * c0 + y) % c1, y % c1, c0 % c1 == 0) ||
               rewrite((y + x * c0) % c1, y % c1, c0 % c1 == 0) ||
               rewrite((x * c0 - y) % c1, (-y) % c1, c0 % c1 == 0) ||
               rewrite((y - x * c0) % c1, y % c1, c0 % c1 == 0) ||
               #ifdef EXCLUDE_INVALID_ORDERING_RULES
               rewrite((x - y) % 2, (x + y) % 2) || // Addition and subtraction are the same modulo 2, because -1 == 1
               #endif
               rewrite(ramp(x, c0) % broadcast(c1), broadcast(x, lanes) % c1, c0 % c1 == 0) ||
               rewrite(ramp(x, c0) % broadcast(c1), ramp(x % c1, c0, lanes),
                       // First and last lanes are the same when...
                       can_prove((x % c1 + c0 * (lanes - 1)) / c1 == 0, this)) ||
               rewrite(ramp(x * c0, c2) % broadcast(c1), (ramp(x * fold(c0 % c1), fold(c2 % c1), lanes) % c1), c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               rewrite(ramp(x + c0, c2) % broadcast(c1), (ramp(x + fold(c0 % c1), fold(c2 % c1), lanes) % c1), c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               rewrite(ramp(x * c0 + y, c2) % broadcast(c1), ramp(y, fold(c2 % c1), lanes) % c1, c0 % c1 == 0) ||
               rewrite(ramp(y + x * c0, c2) % broadcast(c1), ramp(y, fold(c2 % c1), lanes) % c1, c0 % c1 == 0))))) {
            return mutate(std::move(rewrite.result), bounds);
        }

        if (no_overflow_int(op->type) &&
            use_synthesized_rules &&
            (
#include "Simplify_Mod.inc"
             )) {
            return mutate(std::move(rewrite.result), bounds);
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Mod::make(a, b);
    }
}

}
}
