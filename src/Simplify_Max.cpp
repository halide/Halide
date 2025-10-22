#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Max *op, ExprInfo *info) {
    ExprInfo a_info, b_info, max_info;
    Expr a = mutate(op->a, &a_info);
    Expr b = mutate(op->b, &b_info);

    if (op->type.is_int_or_uint()) {
        max_info.bounds = max(a_info.bounds, b_info.bounds);
        max_info.alignment = ModulusRemainder::unify(a_info.alignment, b_info.alignment);
        max_info.trim_bounds_using_alignment();
    }

    if (info) {
        *info = max_info;
    }

    if (max_info.bounds.is_single_point()) {
        // This is possible when, for example, the largest number in the type
        // that satisfies the alignment of the left-hand-side is smaller than
        // the min value of the right-hand-side.
        return make_const(op->type, max_info.bounds.min, nullptr);
    }

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
    if (a_info.bounds <= b_info.bounds) {
        if (info) {
            // We lost information when we unioned the alignment, so revert to the info for b.
            *info = b_info;
        }
        return strip_likely(b);
    }
    if (b_info.bounds <= a_info.bounds) {
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
    auto rewrite = IRMatcher::rewriter(IRMatcher::max(a, b), op->type);

    if (rewrite(max(IRMatcher::Overflow(), x), a) ||
        rewrite(max(x, IRMatcher::Overflow()), b)) {
        clear_expr_info(info);
        return rewrite.result;
    }

    // clang-format off
    if (EVAL_IN_LAMBDA
        (rewrite(max(x, x), a) ||
         rewrite(max(c0, c1), fold(max(c0, c1))) ||
         // Cases where one side dominates:
         rewrite(max(x, c0), b, is_max_value(c0)) ||
         rewrite(max(x, c0), a, is_min_value(c0)) ||
         rewrite(max((x/c0)*c0, x), b, c0 > 0) ||
         rewrite(max(x, (x/c0)*c0), a, c0 > 0) ||
         rewrite(max(max(x, y), x), a) ||
         rewrite(max(max(x, y), y), a) ||
         rewrite(max(max(max(x, y), z), x), a) ||
         rewrite(max(max(max(x, y), z), y), a) ||
         rewrite(max(max(max(max(x, y), z), w), x), a) ||
         rewrite(max(max(max(max(x, y), z), w), y), a) ||
         rewrite(max(max(max(max(max(x, y), z), w), u), x), a) ||
         rewrite(max(max(max(max(max(x, y), z), w), u), y), a) ||
         rewrite(max(x, max(x, y)), b) ||
         rewrite(max(x, min(x, y)), a) ||
         rewrite(max(x, max(y, x)), b) ||
         rewrite(max(x, min(y, x)), a) ||
         rewrite(max(max(x, y), min(x, y)), a) ||
         rewrite(max(max(x, y), min(y, x)), a) ||
         rewrite(max(min(x, y), x), b) ||
         rewrite(max(min(y, x), x), b) ||
         rewrite(max(min(x, c0), c1), b, c1 >= c0) ||

         rewrite(max(x, min(y, min(x, z))), a) ||
         rewrite(max(x, min(y, min(z, x))), a) ||
         rewrite(max(x, min(min(x, y), z)), a) ||
         rewrite(max(x, min(min(y, x), z)), a) ||
         rewrite(max(min(x, min(y, z)), y), b) ||
         rewrite(max(min(x, min(y, z)), z), b) ||
         rewrite(max(min(min(x, y), z), x), b) ||
         rewrite(max(min(min(x, y), z), y), b) ||

         rewrite(max(max(x, y), min(x, z)), a) ||
         rewrite(max(max(x, y), min(y, z)), a) ||
         rewrite(max(max(x, y), min(z, x)), a) ||
         rewrite(max(max(x, y), min(z, y)), a) ||

         rewrite(max(likely(x), x), b) ||
         rewrite(max(x, likely(x)), a) ||
         rewrite(max(likely_if_innermost(x), x), b) ||
         rewrite(max(x, likely_if_innermost(x)), a) ||

         (no_overflow(op->type) &&
          (rewrite(max(ramp(x, y, lanes), broadcast(z, lanes)), a,
                   can_prove(x + y * (lanes - 1) >= z && x >= z, this)) ||
           rewrite(max(ramp(x, y, lanes), broadcast(z, lanes)), b,
                   can_prove(x + y * (lanes - 1) <= z && x <= z, this)) ||
           // Compare x to a stair-step function in x
           rewrite(max(((x + c0)/c1)*c1 + c2, x), a, c1 > 0 && c0 + c2 >= c1 - 1) ||
           rewrite(max(x, ((x + c0)/c1)*c1 + c2), b, c1 > 0 && c0 + c2 >= c1 - 1) ||
           rewrite(max(((x + c0)/c1)*c1 + c2, x), b, c1 > 0 && c0 + c2 <= 0) ||
           rewrite(max(x, ((x + c0)/c1)*c1 + c2), a, c1 > 0 && c0 + c2 <= 0) ||
           rewrite(max((x/c0)*c0, (x/c1)*c1 + c2), b, c2 >= c1 && c1 > 0 && c0 != 0) ||
           // Special cases where c0 or c2 is zero
           rewrite(max((x/c1)*c1 + c2, x), a, c1 > 0 && c2 >= c1 - 1) ||
           rewrite(max(x, (x/c1)*c1 + c2), b, c1 > 0 && c2 >= c1 - 1) ||
           rewrite(max(((x + c0)/c1)*c1, x), a, c1 > 0 && c0 >= c1 - 1) ||
           rewrite(max(x, ((x + c0)/c1)*c1), b, c1 > 0 && c0 >= c1 - 1) ||
           rewrite(max((x/c1)*c1 + c2, x), b, c1 > 0 && c2 <= 0) ||
           rewrite(max(x, (x/c1)*c1 + c2), a, c1 > 0 && c2 <= 0) ||
           rewrite(max(((x + c0)/c1)*c1, x), b, c1 > 0 && c0 <= 0) ||
           rewrite(max(x, ((x + c0)/c1)*c1), a, c1 > 0 && c0 <= 0) ||

           rewrite(max(x, min(x, y) + c0), a, c0 <= 0) ||
           rewrite(max(x, min(y, x) + c0), a, c0 <= 0) ||
           rewrite(max(min(x, y) + c0, x), b, c0 <= 0) ||
           rewrite(max(min(x, y) + c0, y), b, c0 <= 0) ||
           rewrite(max(min(x, y + c0), y), b, c0 <= 0) ||

           (no_overflow_int(op->type) &&
            (rewrite(max(min(c0 - x, x), c1), b, 2*c1 >= c0 - 1) ||
             rewrite(max(min(x, c0 - x), c1), b, 2*c1 >= c0 - 1))) ||

           false)))) {

        if (info) {
            // One of the cancellation rules above may give us tighter bounds
            // than just applying max to two constant intervals.
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
        (rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1)))) ||
         rewrite(max(max(x, c0), y), max(max(x, y), c0)) ||
         rewrite(max(max(x, y), max(x, z)), max(max(y, z), x)) ||
         rewrite(max(max(y, x), max(x, z)), max(max(y, z), x)) ||
         rewrite(max(max(x, y), max(z, x)), max(max(y, z), x)) ||
         rewrite(max(max(y, x), max(z, x)), max(max(y, z), x)) ||
         rewrite(max(max(x, y), max(z, w)), max(max(max(x, y), z), w)) ||
         rewrite(max(broadcast(x, c0), broadcast(y, c0)), broadcast(max(x, y), c0)) ||
         rewrite(max(max(x, broadcast(y, c0)), broadcast(z, c0)), max(x, broadcast(max(y, z), c0))) ||
         rewrite(max(min(x, y), min(x, z)), min(x, max(y, z))) ||
         rewrite(max(min(x, y), min(z, x)), min(x, max(y, z))) ||
         rewrite(max(min(y, x), min(x, z)), min(max(y, z), x)) ||
         rewrite(max(min(y, x), min(z, x)), min(max(y, z), x)) ||
         rewrite(max(min(max(x, y), z), y), max(min(x, z), y)) ||
         rewrite(max(min(max(y, x), z), y), max(y, min(x, z))) ||
         rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1)))) ||

         rewrite(max(max(x / c0, y), z / c0), max(max(x, z) / c0, y), c0 > 0) ||

         rewrite(max(x, select(x == c0, c1, x)), select(x == c0, c1, x), c0 < c1) ||
         rewrite(max(x, select(x == c0, c1, x)), x, c1 <= c0) ||
         rewrite(max(select(x == c0, c1, x), c2), max(x, c2), (c0 <= c2) && (c1 <= c2)) ||
         rewrite(max(select(x == c0, c1, x), x), select(x == c0, c1, x), c0 < c1) ||
         rewrite(max(select(x == c0, c1, x), x), x, c1 <= c0) ||

         rewrite(max(max(x, min(y, z)), y), max(x, y)) ||
         rewrite(max(max(x, min(y, z)), z), max(x, z)) ||
         rewrite(max(max(min(x, y), z), x), max(z, x)) ||
         rewrite(max(max(min(x, y), z), y), max(z, y)) ||
         rewrite(max(x, max(y, min(x, z))), max(y, x)) ||
         rewrite(max(x, max(y, min(z, x))), max(y, x)) ||
         rewrite(max(x, max(min(x, y), z)), max(x, z)) ||
         rewrite(max(x, max(min(y, x), z)), max(x, z)) ||

         rewrite(max(select(x, min(y, z), w), z), select(x, z, max(w, z))) ||
         rewrite(max(select(x, min(z, y), w), z), select(x, z, max(w, z))) ||
         rewrite(max(z, select(x, min(y, z), w)), select(x, z, max(z, w))) ||
         rewrite(max(z, select(x, min(z, y), w)), select(x, z, max(z, w))) ||
         rewrite(max(select(x, y, min(w, z)), z), select(x, max(y, z), z)) ||
         rewrite(max(select(x, y, min(z, w)), z), select(x, max(y, z), z)) ||
         rewrite(max(z, select(x, y, min(w, z))), select(x, max(z, y), z)) ||
         rewrite(max(z, select(x, y, min(z, w))), select(x, max(z, y), z)) ||

         rewrite(max(select(x, y, z), select(x, w, u)), select(x, max(y, w), max(z, u))) ||
         rewrite(max(select(x, max(z, y), w), z), max(select(x, y, w), z)) ||
         rewrite(max(select(x, max(z, y), w), y), max(select(x, z, w), y)) ||
         rewrite(max(select(x, w, max(z, y)), z), max(select(x, w, y), z)) ||
         rewrite(max(select(x, w, max(z, y)), y), max(select(x, w, z), y)) ||

         // Hoist shuffles. The Shuffle visitor wants to sink
         // extract_elements to the leaves, and those count as degenerate
         // slices, so only hoist shuffles that grab more than one lane.
         rewrite(max(slice(x, c0, c1, c2), slice(y, c0, c1, c2)), slice(max(x, y), c0, c1, c2), c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite(max(slice(x, c0, c1, c2), max(slice(y, c0, c1, c2), z)), max(slice(max(x, y), c0, c1, c2), z), c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite(max(slice(x, c0, c1, c2), max(z, slice(y, c0, c1, c2))), max(slice(max(x, y), c0, c1, c2), z), c2 > 1 && lanes_of(x) == lanes_of(y)) ||

         (no_overflow(op->type) &&
          (rewrite(max(max(x, y) + c0, x), max(x, y + c0), c0 < 0) ||
           rewrite(max(max(x, y) + c0, x), max(x, y) + c0, c0 > 0) ||
           rewrite(max(max(y, x) + c0, x), max(y + c0, x), c0 < 0) ||
           rewrite(max(max(y, x) + c0, x), max(y, x) + c0, c0 > 0) ||

           rewrite(max(x, max(x, y) + c0), max(x, y + c0), c0 < 0) ||
           rewrite(max(x, max(x, y) + c0), max(x, y) + c0, c0 > 0) ||
           rewrite(max(x, max(y, x) + c0), max(x, y + c0), c0 < 0) ||
           rewrite(max(x, max(y, x) + c0), max(x, y) + c0, c0 > 0) ||

           rewrite(max(x + c0, c1), max(x, fold(c1 - c0)) + c0) ||

           rewrite(max(x + c0, y + c1), max(x, y + fold(c1 - c0)) + c0, c1 > c0) ||
           rewrite(max(x + c0, y + c1), max(x + fold(c0 - c1), y) + c1, c0 > c1) ||

           rewrite(max(max(x, y), x + c0), max(x + c0, y), c0 > 0) ||
           rewrite(max(max(x, y), x + c0), max(x, y), c0 < 0) ||
           rewrite(max(max(y, x), x + c0), max(y, x + c0), c0 > 0) ||
           rewrite(max(max(y, x), x + c0), max(y, x), c0 < 0) ||

           rewrite(max(x + y, x + z), x + max(y, z)) ||
           rewrite(max(x + y, z + x), x + max(y, z)) ||
           rewrite(max(y + x, x + z), max(y, z) + x) ||
           rewrite(max(y + x, z + x), max(y, z) + x) ||
           rewrite(max(x, x + z), x + max(z, 0)) ||
           rewrite(max(x, z + x), x + max(z, 0)) ||
           rewrite(max(y + x, x), max(y, 0) + x) ||
           rewrite(max(x + y, x), x + max(y, 0)) ||

           rewrite(max((x*c0 + y)*c1, x*c2 + z), max(y*c1, z) + x*c2, c0 * c1 == c2) ||
           rewrite(max((y + x*c0)*c1, x*c2 + z), max(y*c1, z) + x*c2, c0 * c1 == c2) ||
           rewrite(max((x*c0 + y)*c1, z + x*c2), max(y*c1, z) + x*c2, c0 * c1 == c2) ||
           rewrite(max((y + x*c0)*c1, z + x*c2), max(y*c1, z) + x*c2, c0 * c1 == c2) ||

           rewrite(max(max(x + y, z), x + w), max(x + max(y, w), z)) ||
           rewrite(max(max(z, x + y), x + w), max(x + max(y, w), z)) ||
           rewrite(max(max(x + y, z), w + x), max(x + max(y, w), z)) ||
           rewrite(max(max(z, x + y), w + x), max(x + max(y, w), z)) ||

           rewrite(max(max(y + x, z), x + w), max(max(y, w) + x, z)) ||
           rewrite(max(max(z, y + x), x + w), max(max(y, w) + x, z)) ||
           rewrite(max(max(y + x, z), w + x), max(max(y, w) + x, z)) ||
           rewrite(max(max(z, y + x), w + x), max(max(y, w) + x, z)) ||

           rewrite(max((x + w) + y, x + z), x + max(w + y, z)) ||
           rewrite(max((w + x) + y, x + z), max(w + y, z) + x) ||
           rewrite(max((x + w) + y, z + x), x + max(w + y, z)) ||
           rewrite(max((w + x) + y, z + x), max(w + y, z) + x) ||
           rewrite(max((x + w) + y, x), x + max(w + y, 0)) ||
           rewrite(max((w + x) + y, x), x + max(w + y, 0)) ||
           rewrite(max(x + y, (w + x) + z), x + max(w + z, y)) ||
           rewrite(max(x + y, (x + w) + z), x + max(w + z, y)) ||
           rewrite(max(y + x, (w + x) + z), max(w + z, y) + x) ||
           rewrite(max(y + x, (x + w) + z), max(w + z, y) + x) ||
           rewrite(max(x, (w + x) + z), x + max(w + z, 0)) ||
           rewrite(max(x, (x + w) + z), x + max(w + z, 0)) ||

           rewrite(max(y - x, z - x), max(y, z) - x) ||
           rewrite(max(x - y, x - z), x - min(y, z)) ||
           rewrite(max(x - y, (z - y) + w), max(x, z + w) - y) ||
           rewrite(max(x - y, w + (z - y)), max(x, w + z) - y) ||

           rewrite(max(x, x - y), x - min(y, 0)) ||
           rewrite(max(x - y, x), x - min(y, 0)) ||
           rewrite(max(x, (x - y) + z), x + max(z - y, 0)) ||
           rewrite(max(x, z + (x - y)), x + max(z - y, 0)) ||
           rewrite(max(x, (x - y) - z), x - min(y + z, 0)) ||
           rewrite(max((x - y) + z, x), max(z - y, 0) + x) ||
           rewrite(max(z + (x - y), x), max(z - y, 0) + x) ||
           rewrite(max((x - y) - z, x), x - min(y + z, 0)) ||

           rewrite(max(x * c0, c1), max(x, fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
           rewrite(max(x * c0, c1), min(x, fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||

           rewrite(max(x * c0, y * c1), max(x, y * fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
           rewrite(max(x * c0, y * c1), min(x, y * fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||
           rewrite(max(x * c0, y * c1), max(x * fold(c0 / c1), y) * c1, c1 > 0 && c0 % c1 == 0) ||
           rewrite(max(x * c0, y * c1), min(x * fold(c0 / c1), y) * c1, c1 < 0 && c0 % c1 == 0) ||
           rewrite(max(x * c0, y * c0 + c1), max(x, y + fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
           rewrite(max(x * c0, y * c0 + c1), min(x, y + fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||

           rewrite(max(x / c0, y / c0), max(x, y) / c0, c0 > 0) ||
           rewrite(max(x / c0, y / c0), min(x, y) / c0, c0 < 0) ||

           /* Causes some things to cancel, but also creates large constants and breaks peephole patterns
              rewrite(max(x / c0, c1), max(x, fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0)) ||
              rewrite(max(x / c0, c1), min(x, fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0)) ||
           */

           rewrite(max(x / c0, y / c0 + c1), max(x, y + fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0)) ||
           rewrite(max(x / c0, y / c0 + c1), min(x, y + fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0)) ||

           rewrite(max(((x + c0) / c1) * c1, x + c2), ((x + c0) / c1) * c1, c1 > 0 && c0 + 1 >= c1 + c2) ||

           rewrite(max((x + c0)/c1, ((x + c2)/c3)*c4), (x + c0)/c1, c2 <= c0 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(max((x + c0)/c1, ((x + c2)/c3)*c4), ((x + c2)/c3)*c4, c0 + c3 - c1 <= c2 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(max(x/c1, ((x + c2)/c3)*c4), x/c1, c2 <= 0 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(max(x/c1, ((x + c2)/c3)*c4), ((x + c2)/c3)*c4, c3 - c1 <= c2 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(max((x + c0)/c1, (x/c3)*c4), (x + c0)/c1, 0 <= c0 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(max((x + c0)/c1, (x/c3)*c4), (x/c3)*c4, c0 + c3 - c1 <= 0 && c1 > 0 && c3 > 0 && c1 * c4 == c3) ||
           rewrite(max(x/c1, (x/c3)*c4), x/c1, c1 > 0 && c3 > 0 && c1 * c4 == c3) ||

           rewrite(max(c0 - x, c1), c0 - min(x, fold(c0 - c1))))))) {

        return mutate(rewrite.result, info);
    }
    // clang-format on

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Max::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
