#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Min *op, ExprInfo *info) {
    ExprInfo min_info, a_info, b_info;
    Expr a = mutate(op->a, &a_info);
    Expr b = mutate(op->b, &b_info);

    if (op->type.is_int_or_uint()) {
        min_info.bounds = min(a_info.bounds, b_info.bounds);
        min_info.alignment = ModulusRemainder::unify(a_info.alignment, b_info.alignment);
        min_info.trim_bounds_using_alignment();
    }

    if (info) {
        *info = min_info;
    }

    if (min_info.bounds.is_single_point()) {
        // This is possible when, for example, the smallest number in the type
        // that satisfies the alignment of the left-hand-side is greater than
        // the max value of the right-hand-side.
        return make_const(op->type, min_info.bounds.min, nullptr);
    }

    // Early out when the bounds tells us one side or the other is smaller
    auto strip_likely = [](const Expr &e) {
        if (const Call *call = e.as<Call>()) {
            if (call->is_intrinsic(Call::likely) ||
                call->is_intrinsic(Call::likely_if_innermost)) {
                return call->args[0];
            }
        }
        return e;
    };

    // Early out when the bounds tells us one side or the other is smaller
    if (a_info.bounds >= b_info.bounds) {
        if (info) {
            // We lost information when we unioned the alignment, so revert to the info for b.
            *info = b_info;
        }
        return strip_likely(b);
    }
    if (b_info.bounds >= a_info.bounds) {
        if (info) {
            *info = a_info;
        }
        return strip_likely(a);
    }

    // Order commutative operations by node type
    if (should_commute(a, b)) {
        std::swap(a, b);
        std::swap(a_info, b_info);
    }

    int lanes = op->type.lanes();
    auto rewrite = IRMatcher::rewriter(IRMatcher::min(a, b), op->type);

    if (rewrite(min(IRMatcher::Overflow(), x), a) ||
        rewrite(min(x, IRMatcher::Overflow()), b)) {
        clear_expr_info(info);
        return rewrite.result;
    }

    // clang-format off
    if (EVAL_IN_LAMBDA
        (rewrite(min(x, x), a) ||
         rewrite(min(c0, c1), fold(min(c0, c1))) ||
         // Cases where one side dominates:
         rewrite(min(x, c0), b, is_min_value(c0)) ||
         rewrite(min(x, c0), a, is_max_value(c0)) ||
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
         rewrite(min(x, min(x, y)), b) ||
         rewrite(min(x, max(x, y)), a) ||
         rewrite(min(x, min(y, x)), b) ||
         rewrite(min(x, max(y, x)), a) ||
         rewrite(min(max(x, y), min(x, y)), b) ||
         rewrite(min(max(x, y), min(y, x)), b) ||
         rewrite(min(max(x, y), x), b) ||
         rewrite(min(max(y, x), x), b) ||
         rewrite(min(max(x, c0), c1), b, c1 <= c0) ||

         rewrite(min(x, max(y, max(x, z))), a) ||
         rewrite(min(x, max(y, max(z, x))), a) ||
         rewrite(min(x, max(max(x, y), z)), a) ||
         rewrite(min(x, max(max(y, x), z)), a) ||
         rewrite(min(max(x, max(y, z)), y), b) ||
         rewrite(min(max(x, max(y, z)), z), b) ||
         rewrite(min(max(max(x, y), z), x), b) ||
         rewrite(min(max(max(x, y), z), y), b) ||

         rewrite(min(max(x, y), min(x, z)), b) ||
         rewrite(min(max(x, y), min(y, z)), b) ||
         rewrite(min(max(x, y), min(z, x)), b) ||
         rewrite(min(max(x, y), min(z, y)), b) ||

         rewrite(min(likely(x), x), b) ||
         rewrite(min(x, likely(x)), a) ||
         rewrite(min(likely_if_innermost(x), x), b) ||
         rewrite(min(x, likely_if_innermost(x)), a) ||

         (no_overflow(op->type) &&
          (rewrite(min(ramp(x, y, lanes), broadcast(z, lanes)), a,
                   can_prove(x + y * (lanes - 1) <= z && x <= z, this)) ||
           rewrite(min(ramp(x, y, lanes), broadcast(z, lanes)), b,
                   can_prove(x + y * (lanes - 1) >= z && x >= z, this)) ||
           // Compare x to a stair-step function in x
           rewrite(min(((x + c0)/c1)*c1 + c2, x), b, c1 > 0 && c0 + c2 >= c1 - 1) ||
           rewrite(min(x, ((x + c0)/c1)*c1 + c2), a, c1 > 0 && c0 + c2 >= c1 - 1) ||
           rewrite(min(((x + c0)/c1)*c1 + c2, x), a, c1 > 0 && c0 + c2 <= 0) ||
           rewrite(min(x, ((x + c0)/c1)*c1 + c2), b, c1 > 0 && c0 + c2 <= 0) ||
           rewrite(min((x/c0)*c0, (x/c1)*c1 + c2), a, c2 >= c1 && c1 > 0 && c0 != 0) ||
           // Special cases where c0 or c2 is zero
           rewrite(min((x/c1)*c1 + c2, x), b, c1 > 0 && c2 >= c1 - 1) ||
           rewrite(min(x, (x/c1)*c1 + c2), a, c1 > 0 && c2 >= c1 - 1) ||
           rewrite(min(((x + c0)/c1)*c1, x), b, c1 > 0 && c0 >= c1 - 1) ||
           rewrite(min(x, ((x + c0)/c1)*c1), a, c1 > 0 && c0 >= c1 - 1) ||
           rewrite(min((x/c1)*c1 + c2, x), a, c1 > 0 && c2 <= 0) ||
           rewrite(min(x, (x/c1)*c1 + c2), b, c1 > 0 && c2 <= 0) ||
           rewrite(min(((x + c0)/c1)*c1, x), a, c1 > 0 && c0 <= 0) ||
           rewrite(min(x, ((x + c0)/c1)*c1), b, c1 > 0 && c0 <= 0) ||

           rewrite(min(x, max(x, y) + c0), a, 0 <= c0) ||
           rewrite(min(x, max(y, x) + c0), a, 0 <= c0) ||
           rewrite(min(max(x, y) + c0, x), b, 0 <= c0) ||
           rewrite(min(max(x, y) + c0, y), b, 0 <= c0) ||
           rewrite(min(max(x, y + c0), y), b, 0 <= c0) ||

           (no_overflow_int(op->type) &&
            (rewrite(min(max(c0 - x, x), c1), b, 2*c1 <= c0 + 1) ||
             rewrite(min(max(x, c0 - x), c1), b, 2*c1 <= c0 + 1))) ||

           false)))) {
        // One of the cancellation rules above may give us tighter bounds
        // than just applying min to two constant intervals.
        if (info) {
            if (rewrite.result.same_as(a)) {
                info->intersect(a_info);
            } else if (rewrite.result.same_as(b)) {
                info->intersect(b_info);
            }
        }
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
         rewrite(min(broadcast(x, c0), broadcast(y, c0)), broadcast(min(x, y), c0)) ||
         rewrite(min(min(x, broadcast(y, c0)), broadcast(z, c0)), min(x, broadcast(min(y, z), c0))) ||
         rewrite(min(max(x, y), max(x, z)), max(x, min(y, z))) ||
         rewrite(min(max(x, y), max(z, x)), max(x, min(y, z))) ||
         rewrite(min(max(y, x), max(x, z)), max(min(y, z), x)) ||
         rewrite(min(max(y, x), max(z, x)), max(min(y, z), x)) ||
         rewrite(min(max(min(x, y), z), y), min(max(x, z), y)) ||
         rewrite(min(max(min(y, x), z), y), min(y, max(x, z))) ||
         rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1)))) ||

         rewrite(min(min(x / c0, y), z / c0), min(min(x, z) / c0, y), c0 > 0) ||

         // Canonicalize a clamp
         rewrite(min(max(x, c0), c1), max(min(x, c1), c0), c0 <= c1) ||

         rewrite(min(x, select(x == c0, c1, x)), select(x == c0, c1, x), c1 < c0) ||
         rewrite(min(x, select(x == c0, c1, x)), x, c0 <= c1) ||
         rewrite(min(select(x == c0, c1, x), c2), min(x, c2), (c2 <= c0) && (c2 <= c1)) ||
         rewrite(min(select(x == c0, c1, x), x), select(x == c0, c1, x), c1 < c0) ||
         rewrite(min(select(x == c0, c1, x), x), x, c0 <= c1) ||

         rewrite(min(x, min(y, max(x, z))), min(y, x)) ||
         rewrite(min(x, min(y, max(z, x))), min(y, x)) ||
         rewrite(min(x, min(max(x, y), z)), min(x, z)) ||
         rewrite(min(x, min(max(y, x), z)), min(x, z)) ||
         rewrite(min(min(x, max(y, z)), y), min(x, y)) ||
         rewrite(min(min(x, max(y, z)), z), min(x, z)) ||
         rewrite(min(min(max(x, y), z), x), min(z, x)) ||
         rewrite(min(min(max(x, y), z), y), min(z, y)) ||

         rewrite(min(select(x, max(y, z), w), z), select(x, z, min(w, z))) ||
         rewrite(min(select(x, max(z, y), w), z), select(x, z, min(w, z))) ||
         rewrite(min(z, select(x, max(y, z), w)), select(x, z, min(z, w))) ||
         rewrite(min(z, select(x, max(z, y), w)), select(x, z, min(z, w))) ||
         rewrite(min(select(x, y, max(w, z)), z), select(x, min(y, z), z)) ||
         rewrite(min(select(x, y, max(z, w)), z), select(x, min(y, z), z)) ||
         rewrite(min(z, select(x, y, max(w, z))), select(x, min(z, y), z)) ||
         rewrite(min(z, select(x, y, max(z, w))), select(x, min(z, y), z)) ||

         rewrite(min(select(x, y, z), select(x, w, u)), select(x, min(y, w), min(z, u))) ||
         rewrite(min(select(x, min(z, y), w), y), min(select(x, z, w), y)) ||
         rewrite(min(select(x, min(z, y), w), z), min(select(x, y, w), z)) ||
         rewrite(min(select(x, w, min(z, y)), y), min(select(x, w, z), y)) ||
         rewrite(min(select(x, w, min(z, y)), z), min(select(x, w, y), z)) ||

         // Hoist shuffles. The Shuffle visitor wants to sink
         // extract_elements to the leaves, and those count as degenerate
         // slices, so only hoist shuffles that grab more than one lane.
         rewrite(min(slice(x, c0, c1, c2), slice(y, c0, c1, c2)), slice(min(x, y), c0, c1, c2), c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite(min(slice(x, c0, c1, c2), min(slice(y, c0, c1, c2), z)), min(slice(min(x, y), c0, c1, c2), z), c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite(min(slice(x, c0, c1, c2), min(z, slice(y, c0, c1, c2))), min(slice(min(x, y), c0, c1, c2), z), c2 > 1 && lanes_of(x) == lanes_of(y)) ||
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

           rewrite(min(min(x, y), x + c0), min(x, y), c0 > 0) ||
           rewrite(min(min(x, y), x + c0), min(x + c0, y), c0 < 0) ||
           rewrite(min(min(y, x), x + c0), min(y, x), c0 > 0) ||
           rewrite(min(min(y, x), x + c0), min(y, x + c0), c0 < 0) ||

           rewrite(min(max(x + c0, y), x), x, c0 > 0) ||

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
           rewrite(min(x - y, (z - y) + w), min(x, z + w) - y) ||
           rewrite(min(x - y, w + (z - y)), min(x, w + z) - y) ||

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

           rewrite(min(((x + c0) / c1) * c1, x + c2), x + c2, c1 > 0 && c0 + 1 >= c1 + c2) ||

           rewrite(min(c0 - x, c1), c0 - max(x, fold(c0 - c1))) ||

           // Required for nested GuardWithIf tilings
           rewrite(min((min(((y + c0)/c1), x)*c1), y + c2), min(x * c1, y + c2), c1 > 0 && c1 + c2 <= c0 + 1) ||
           rewrite(min((min(((y + c0)/c1), x)*c1) + c2, y), min(x * c1 + c2, y), c1 > 0 && c1 <= c0 + c2 + 1) ||
           rewrite(min(min(((y + c0)/c1), x)*c1, y), min(x * c1, y), c1 > 0 && c1 <= c0 + 1) ||

           rewrite(min((x + c0)/c1, ((x + c2)/c3)*c4), (x + c0)/c1, c0 + c3 - c1 <= c2 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(min((x + c0)/c1, ((x + c2)/c3)*c4), ((x + c2)/c3)*c4, c2 <= c0 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(min(x/c1, ((x + c2)/c3)*c4), x/c1, c3 - c1 <= c2 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(min(x/c1, ((x + c2)/c3)*c4), ((x + c2)/c3)*c4, c2 <= 0 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(min((x + c0)/c1, (x/c3)*c4), (x + c0)/c1, c0 + c3 - c1 <= 0 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(min((x + c0)/c1, (x/c3)*c4), (x/c3)*c4, 0 <= c0 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(min(x/c1, (x/c3)*c4), (x/c3)*c4, c1 > 0 && c3 > 0 && c1 * c4 == c3) ||

           false )))) {

        return mutate(rewrite.result, info);
    }
    // clang-format on

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Min::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
