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

    if (no_overflow_int(op->type)) {
        const bool b_positive = (b_bounds.min_defined && b_bounds.min > 0);
        const bool b_negative = (b_bounds.max_defined && b_bounds.max < 0);
        const bool b_non_zero =
            (b_positive || b_negative || b_bounds.alignment.remainder != 0);

        if (b_non_zero) {
            // If b is non-zero, the the result is at least zero.
            mod_bounds.min_defined = true;
            mod_bounds.min = 0;

            // Mod by something non-zero produces a result between 0
            // and abs(modulus) - 1. However, if b is unbounded in
            // either direction, abs(modulus) could be arbitrarily
            // large.
            if (b_bounds.max_defined && b_bounds.min_defined) {
                // b crosses zero but isn't zero
                mod_bounds.max_defined = true;
                mod_bounds.max = std::max(b_bounds.max - 1, // b > 0
                                          1 - b_bounds.min); // b < 0
            }
        } else {
            if (a_bounds.min_defined) {
                // Even if b is zero, mod can't make something more negative.
                mod_bounds.min_defined = true;
                mod_bounds.min = std::min(a_bounds.min, (int64_t)0);
            }

            if (b_bounds.min_defined && b_bounds.max_defined && a_bounds.max_defined) {
                // b is bounded and spans zero, and a has an upper
                // bound. The result has an upper bound.
                mod_bounds.max_defined = true;
                mod_bounds.max =
                    std::max(a_bounds.max, // Achieved when b == 0
                             std::max(b_bounds.max - 1, // b > 0
                                      1 - b_bounds.min)); // b < 0
            }
        }

        mod_bounds.alignment = a_bounds.alignment % b_bounds.alignment;
        mod_bounds.trim_bounds_using_alignment();
        if (bounds) {
            *bounds = mod_bounds;
        }
    }

    if (may_simplify(op->type)) {
        if (a_bounds.min_defined && a_bounds.min >= 0 &&
            a_bounds.max_defined && b_bounds.min_defined && a_bounds.max < b_bounds.min) {
            if (bounds) {
                *bounds = a_bounds;
            }
            return a;
        }

        if (mod_bounds.min_defined && mod_bounds.max_defined && mod_bounds.min == mod_bounds.max) {
            return make_const(op->type, mod_bounds.min);
        }

        int lanes = op->type.lanes();

        auto rewrite = IRMatcher::rewriter(IRMatcher::mod(a, b), op->type);

        if (rewrite(c0 % c1, fold(c0 % c1)) ||
            rewrite(IRMatcher::Overflow() % x, a) ||
            rewrite(x % IRMatcher::Overflow(), b) ||
            rewrite(0 % x, 0) ||
            rewrite(x % x, 0) ||
            (!op->type.is_float() &&
             (rewrite(x % 0, x) ||
              rewrite(x % 1, 0)))) {
            return rewrite.result;
        }

        if (EVAL_IN_LAMBDA
            (rewrite(broadcast(x) % broadcast(y), broadcast(x % y, lanes)) ||
             (no_overflow_int(op->type) &&
              (rewrite((x * c0) % c1, (x * fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               rewrite((x + c0) % c1, (x + fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               rewrite((x * c0) % c1, (x % fold(c1/c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
               rewrite((x * c0 + y) % c1, y % c1, c0 % c1 == 0) ||
               rewrite((y + x * c0) % c1, y % c1, c0 % c1 == 0) ||
               rewrite((x * c0 - y) % c1, (-y) % c1, c0 % c1 == 0) ||
               rewrite((y - x * c0) % c1, y % c1, c0 % c1 == 0) ||
               rewrite((x - y) % 2, (x + y) % 2) || // Addition and subtraction are the same modulo 2, because -1 == 1

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
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Mod::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
