#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Min *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds) {
        bounds->min_defined = a_bounds.min_defined && b_bounds.min_defined;
        bounds->max_defined = a_bounds.max_defined || b_bounds.max_defined;
        bounds->min = std::min(a_bounds.min, b_bounds.min);
        if (a_bounds.max_defined && b_bounds.max_defined) {
            bounds->max = std::min(a_bounds.max, b_bounds.max);
        } else if (a_bounds.max_defined) {
            bounds->max = a_bounds.max;
        } else {
            bounds->max = b_bounds.max;
        }
        bounds->alignment = ModulusRemainder::unify(a_bounds.alignment, b_bounds.alignment);
        bounds->trim_bounds_using_alignment();
    }

    // Early out when the bounds tells us one side or the other is smaller
    if (a_bounds.max_defined && b_bounds.min_defined && a_bounds.max <= b_bounds.min) {
        if (const Call *call = a.as<Call>()) {
            if (call->is_intrinsic(Call::likely) ||
                call->is_intrinsic(Call::likely_if_innermost)) {
                return call->args[0];
            }
        }
        return a;
    }
    if (b_bounds.max_defined && a_bounds.min_defined && b_bounds.max <= a_bounds.min) {
        if (const Call *call = b.as<Call>()) {
            if (call->is_intrinsic(Call::likely) ||
                call->is_intrinsic(Call::likely_if_innermost)) {
                return call->args[0];
            }
        }
        return b;
    }

    if (may_simplify(op->type)) {

        // Order commutative operations by node type
        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_bounds, b_bounds);
        }

        int lanes = op->type.lanes();
        auto rewrite = IRMatcher::rewriter(IRMatcher::min(a, b), op->type);

        // clang-format off
        if (EVAL_IN_LAMBDA
            (rewrite(min(x, x), x) ||
             rewrite(min(c0, c1), fold(min(c0, c1))) ||
             rewrite(min(IRMatcher::Overflow(), x), a) ||
             rewrite(min(x,IRMatcher::Overflow()), b) ||
             // Cases where one side dominates:
             rewrite(min(x, op->type.min()), b) ||
             rewrite(min(x, op->type.max()), x) ||
             rewrite(min((x/c0)*c0, x), a, c0 > 0) ||
             rewrite(min(x, (x/c0)*c0), b, c0 > 0) ||
             rewrite(min(min(x, y), x), a) ||
             rewrite(min(min(x, y), y), a) ||
             rewrite(min(min(min(x, y), z), x), a) ||
             rewrite(min(min(min(x, y), z), y), a) ||
             rewrite(min(min(min(min(x, y), z), w), x), a) ||
             rewrite(min(min(min(min(x, y), z), w), y), a) ||
             rewrite(min(min(min(min(min(x, y), z), w), u), x), a) ||
             rewrite(min(min(min(min(min(x, y), z), w), u), y), a) ||
             rewrite(min(x, max(x, y)), a) ||
             rewrite(min(x, max(y, x)), a) ||
             rewrite(min(max(x, y), min(x, y)), b) ||
             rewrite(min(max(x, y), min(y, x)), b) ||
             rewrite(min(max(x, y), x), b) ||
             rewrite(min(max(y, x), x), b) ||
             rewrite(min(max(x, c0), c1), b, c1 <= c0) ||

             rewrite(min(intrin(Call::likely, x), x), b) ||
             rewrite(min(x, intrin(Call::likely, x)), a) ||
             rewrite(min(intrin(Call::likely_if_innermost, x), x), b) ||
             rewrite(min(x, intrin(Call::likely_if_innermost, x)), a) ||

             (no_overflow(op->type) &&
              (rewrite(min(ramp(x, y), broadcast(z)), a, can_prove(x + y * (lanes - 1) <= z && x <= z, this)) ||
               rewrite(min(ramp(x, y), broadcast(z)), b, can_prove(x + y * (lanes - 1) >= z && x >= z, this)) ||
               // Compare x to a stair-step function in x
               rewrite(min(((x + c0)/c1)*c1 + c2, x), b, c1 > 0 && c0 + c2 >= c1 - 1) ||
               rewrite(min(x, ((x + c0)/c1)*c1 + c2), a, c1 > 0 && c0 + c2 >= c1 - 1) ||
               rewrite(min(((x + c0)/c1)*c1 + c2, x), a, c1 > 0 && c0 + c2 <= 0) ||
               rewrite(min(x, ((x + c0)/c1)*c1 + c2), b, c1 > 0 && c0 + c2 <= 0) ||
               // Special cases where c0 or c2 is zero
               rewrite(min((x/c1)*c1 + c2, x), b, c1 > 0 && c2 >= c1 - 1) ||
               rewrite(min(x, (x/c1)*c1 + c2), a, c1 > 0 && c2 >= c1 - 1) ||
               rewrite(min(((x + c0)/c1)*c1, x), b, c1 > 0 && c0 >= c1 - 1) ||
               rewrite(min(x, ((x + c0)/c1)*c1), a, c1 > 0 && c0 >= c1 - 1) ||
               rewrite(min((x/c1)*c1 + c2, x), a, c1 > 0 && c2 <= 0) ||
               rewrite(min(x, (x/c1)*c1 + c2), b, c1 > 0 && c2 <= 0) ||
               rewrite(min(((x + c0)/c1)*c1, x), a, c1 > 0 && c0 <= 0) ||
               rewrite(min(x, ((x + c0)/c1)*c1), b, c1 > 0 && c0 <= 0))))) {
            return rewrite.result;
        }
        // clang-format on

        // clang-format off
        if (EVAL_IN_LAMBDA
            (rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1)))) ||
             rewrite(min(min(x, c0), y), min(min(x, y), c0)) ||
             rewrite(min(min(x, y), min(x, z)), min(min(y, z), x)) ||
             rewrite(min(min(y, x), min(x, z)), min(min(y, z), x)) ||
             rewrite(min(min(x, y), min(z, x)), min(min(y, z), x)) ||
             rewrite(min(min(y, x), min(z, x)), min(min(y, z), x)) ||
             rewrite(min(min(x, y), min(z, w)), min(min(min(x, y), z), w)) ||
             rewrite(min(broadcast(x), broadcast(y)), broadcast(min(x, y), lanes)) ||
             rewrite(min(min(x, broadcast(y)), broadcast(z)), min(x, broadcast(min(y, z), lanes))) ||
             rewrite(min(max(x, y), max(x, z)), max(x, min(y, z))) ||
             rewrite(min(max(x, y), max(z, x)), max(x, min(y, z))) ||
             rewrite(min(max(y, x), max(x, z)), max(min(y, z), x)) ||
             rewrite(min(max(y, x), max(z, x)), max(min(y, z), x)) ||
             rewrite(min(max(min(x, y), z), y), min(max(x, z), y)) ||
             rewrite(min(max(min(y, x), z), y), min(y, max(x, z))) ||
             rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1)))) ||

             // Canonicalize a clamp
             rewrite(min(max(x, c0), c1), max(min(x, c1), c0), c0 <= c1) ||

             rewrite(min(x, select(x == c0, c1, x)), select(x == c0, c1, x), c1 < c0) ||
             rewrite(min(x, select(x == c0, c1, x)), x, c0 <= c1) ||
             rewrite(min(select(x == c0, c1, x), c2), min(x, c2), (c2 <= c0) && (c2 <= c1)) ||
             rewrite(min(select(x == c0, c1, x), x), select(x == c0, c1, x), c1 < c0) ||
             rewrite(min(select(x == c0, c1, x), x), x, c0 <= c1) ||

             (no_overflow(op->type) &&
              (rewrite(min(min(x, y) + c0, x), min(x, y + c0), c0 > 0) ||
               rewrite(min(min(x, y) + c0, x), min(x, y) + c0, c0 < 0) ||
               rewrite(min(min(y, x) + c0, x), min(y + c0, x), c0 > 0) ||
               rewrite(min(min(y, x) + c0, x), min(y, x) + c0, c0 < 0) ||

               rewrite(min(x, min(x, y) + c0), min(x, y + c0), c0 > 0) ||
               rewrite(min(x, min(x, y) + c0), min(x, y) + c0, c0 < 0) ||
               rewrite(min(x, min(y, x) + c0), min(x, y + c0), c0 > 0) ||
               rewrite(min(x, min(y, x) + c0), min(x, y) + c0, c0 < 0) ||

               rewrite(min(x + c0, c1), min(x, fold(c1 - c0)) + c0) ||

               rewrite(min(x + c0, y + c1), min(x, y + fold(c1 - c0)) + c0, c1 > c0) ||
               rewrite(min(x + c0, y + c1), min(x + fold(c0 - c1), y) + c1, c0 > c1) ||

               rewrite(min(x + y, x + z), x + min(y, z)) ||
               rewrite(min(x + y, z + x), x + min(y, z)) ||
               rewrite(min(y + x, x + z), min(y, z) + x) ||
               rewrite(min(y + x, z + x), min(y, z) + x) ||
               rewrite(min(x, x + z), x + min(z, 0)) ||
               rewrite(min(x, z + x), x + min(z, 0)) ||
               rewrite(min(y + x, x), min(y, 0) + x) ||
               rewrite(min(x + y, x), x + min(y, 0)) ||

               rewrite(min((x*c0 + y)*c1, x*c2 + z), min(y*c1, z) + x*c2, c0 * c1 == c2) ||
               rewrite(min((y + x*c0)*c1, x*c2 + z), min(y*c1, z) + x*c2, c0 * c1 == c2) ||
               rewrite(min((x*c0 + y)*c1, z + x*c2), min(y*c1, z) + x*c2, c0 * c1 == c2) ||
               rewrite(min((y + x*c0)*c1, z + x*c2), min(y*c1, z) + x*c2, c0 * c1 == c2) ||

               rewrite(min(min(x + y, z), x + w), min(x + min(y, w), z)) ||
               rewrite(min(min(z, x + y), x + w), min(x + min(y, w), z)) ||
               rewrite(min(min(x + y, z), w + x), min(x + min(y, w), z)) ||
               rewrite(min(min(z, x + y), w + x), min(x + min(y, w), z)) ||

               rewrite(min(min(y + x, z), x + w), min(min(y, w) + x, z)) ||
               rewrite(min(min(z, y + x), x + w), min(min(y, w) + x, z)) ||
               rewrite(min(min(y + x, z), w + x), min(min(y, w) + x, z)) ||
               rewrite(min(min(z, y + x), w + x), min(min(y, w) + x, z)) ||

               rewrite(min((x + w) + y, x + z), x + min(w + y, z)) ||
               rewrite(min((w + x) + y, x + z), min(w + y, z) + x) ||
               rewrite(min((x + w) + y, z + x), x + min(w + y, z)) ||
               rewrite(min((w + x) + y, z + x), min(w + y, z) + x) ||
               rewrite(min((x + w) + y, x), x + min(w + y, 0)) ||
               rewrite(min((w + x) + y, x), x + min(w + y, 0)) ||
               rewrite(min(x + y, (w + x) + z), x + min(w + z, y)) ||
               rewrite(min(x + y, (x + w) + z), x + min(w + z, y)) ||
               rewrite(min(y + x, (w + x) + z), min(w + z, y) + x) ||
               rewrite(min(y + x, (x + w) + z), min(w + z, y) + x) ||
               rewrite(min(x, (w + x) + z), x + min(w + z, 0)) ||
               rewrite(min(x, (x + w) + z), x + min(w + z, 0)) ||

               rewrite(min(y - x, z - x), min(y, z) - x) ||
               rewrite(min(x - y, x - z), x - max(y, z)) ||

               rewrite(min(x, x - y), x - max(y, 0)) ||
               rewrite(min(x - y, x), x - max(y, 0)) ||
               rewrite(min(x, (x - y) + z), x + min(z - y, 0)) ||
               rewrite(min(x, z + (x - y)), x + min(z - y, 0)) ||
               rewrite(min(x, (x - y) - z), x - max(y + z, 0)) ||
               rewrite(min((x - y) + z, x), min(z - y, 0) + x) ||
               rewrite(min(z + (x - y), x), min(z - y, 0) + x) ||
               rewrite(min((x - y) - z, x), x - max(y + z, 0)) ||

               rewrite(min(x * c0, c1), min(x, fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
               rewrite(min(x * c0, c1), max(x, fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||

               rewrite(min(x * c0, y * c1), min(x, y * fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
               rewrite(min(x * c0, y * c1), max(x, y * fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||
               rewrite(min(x * c0, y * c1), min(x * fold(c0 / c1), y) * c1, c1 > 0 && c0 % c1 == 0) ||
               rewrite(min(x * c0, y * c1), max(x * fold(c0 / c1), y) * c1, c1 < 0 && c0 % c1 == 0) ||
               rewrite(min(x * c0, y * c0 + c1), min(x, y + fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
               rewrite(min(x * c0, y * c0 + c1), max(x, y + fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||

               rewrite(min(x / c0, y / c0), min(x, y) / c0, c0 > 0) ||
               rewrite(min(x / c0, y / c0), max(x, y) / c0, c0 < 0) ||

               /* Causes some things to cancel, but also creates large constants and breaks peephole patterns
               rewrite(min(x / c0, c1), min(x, fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0)) ||
               rewrite(min(x / c0, c1), max(x, fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0)) ||
               */

               rewrite(min(x / c0, y / c0 + c1), min(x, y + fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0)) ||
               rewrite(min(x / c0, y / c0 + c1), max(x, y + fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0)) ||

               rewrite(min(select(x, y, z), select(x, w, u)), select(x, min(y, w), min(z, u))) ||

               rewrite(min(c0 - x, c1), c0 - max(x, fold(c0 - c1))) ||

               // Required for nested GuardWithIf tilings
               rewrite(min((min(((y + c0)/c1), x)*c1), y + c2), min(x * c1, y + c2), c1 > 0 && c1 + c2 <= c0 + 1) ||
               rewrite(min((min(((y + c0)/c1), x)*c1) + c2, y), min(x * c1 + c2, y), c1 > 0 && c1 <= c0 + c2 + 1) ||

               false )))) {

            return mutate(rewrite.result, bounds);
        }
        // clang-format on
    }

    const Shuffle *shuffle_a = a.as<Shuffle>();
    const Shuffle *shuffle_b = b.as<Shuffle>();
    if (shuffle_a && shuffle_b &&
        shuffle_a->is_slice() &&
        shuffle_b->is_slice()) {
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return hoist_slice_vector<Min>(op);
        } else {
            return hoist_slice_vector<Min>(Min::make(a, b));
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Min::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
