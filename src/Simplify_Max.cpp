#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Max *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds) {
        bounds->min_defined = a_bounds.min_defined || b_bounds.min_defined;
        bounds->max_defined = a_bounds.max_defined && b_bounds.max_defined;
        bounds->max = std::max(a_bounds.max, b_bounds.max);
        if (a_bounds.min_defined && b_bounds.min_defined) {
            bounds->min = std::max(a_bounds.min, b_bounds.min);
        } else if (a_bounds.min_defined) {
            bounds->min = a_bounds.min;
        } else {
            bounds->min = b_bounds.min;
        }
        bounds->alignment = ModulusRemainder::unify(a_bounds.alignment, b_bounds.alignment);
        bounds->trim_bounds_using_alignment();
    }

    // Early out when the bounds tells us one side or the other is smaller
    if (a_bounds.max_defined && b_bounds.min_defined && a_bounds.max <= b_bounds.min) {
        return b;
    }
    if (b_bounds.max_defined && a_bounds.min_defined && b_bounds.max <= a_bounds.min) {
        return a;
    }

    if (may_simplify(op->type)) {

        // Order commutative operations by node type
        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_bounds, b_bounds);
        }

        int lanes = op->type.lanes();
        auto rewrite = IRMatcher::rewriter(IRMatcher::max(a, b), op->type);

        if (EVAL_IN_LAMBDA
            (rewrite(max(x, x), x) ||
             rewrite(max(c0, c1), fold(max(c0, c1))) ||
             rewrite(max(IRMatcher::Indeterminate(), x), a) ||
             rewrite(max(x, IRMatcher::Indeterminate()), b) ||
             rewrite(max(IRMatcher::Overflow(), x), a) ||
             rewrite(max(x,IRMatcher::Overflow()), b) ||
             // Cases where one side dominates:
             rewrite(max(x, op->type.max()), b) ||
             rewrite(max(x, op->type.min()), x) ||
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
             rewrite(max(x, min(x, y)), a) ||
             rewrite(max(x, min(y, x)), a) ||
             rewrite(max(max(x, y), min(x, y)), a) ||
             rewrite(max(max(x, y), min(y, x)), a) ||
             rewrite(max(min(x, y), x), b) ||
             rewrite(max(min(y, x), x), b) ||
             rewrite(max(min(x, c0), c1), b, c1 >= c0) ||

             rewrite(max(intrin(Call::likely, x), x), a) ||
             rewrite(max(x, intrin(Call::likely, x)), b) ||
             rewrite(max(intrin(Call::likely_if_innermost, x), x), a) ||
             rewrite(max(x, intrin(Call::likely_if_innermost, x)), b) ||

             (no_overflow(op->type) &&
              (rewrite(max(ramp(x, y), broadcast(z)), a, can_prove(x + y * (lanes - 1) >= z && x >= z, this)) ||
               rewrite(max(ramp(x, y), broadcast(z)), b, can_prove(x + y * (lanes - 1) <= z && x <= z, this)) ||
               // Compare x to a stair-step function in x
               rewrite(max(((x + c0)/c1)*c1 + c2, x), a, c1 > 0 && c0 + c2 >= c1 - 1) ||
               rewrite(max(x, ((x + c0)/c1)*c1 + c2), b, c1 > 0 && c0 + c2 >= c1 - 1) ||
               rewrite(max(((x + c0)/c1)*c1 + c2, x), b, c1 > 0 && c0 + c2 <= 0) ||
               rewrite(max(x, ((x + c0)/c1)*c1 + c2), a, c1 > 0 && c0 + c2 <= 0) ||
               // Special cases where c0 or c2 is zero
               rewrite(max((x/c1)*c1 + c2, x), a, c1 > 0 && c2 >= c1 - 1) ||
               rewrite(max(x, (x/c1)*c1 + c2), b, c1 > 0 && c2 >= c1 - 1) ||
               rewrite(max(((x + c0)/c1)*c1, x), a, c1 > 0 && c0 >= c1 - 1) ||
               rewrite(max(x, ((x + c0)/c1)*c1), b, c1 > 0 && c0 >= c1 - 1) ||
               rewrite(max((x/c1)*c1 + c2, x), b, c1 > 0 && c2 <= 0) ||
               rewrite(max(x, (x/c1)*c1 + c2), a, c1 > 0 && c2 <= 0) ||
               rewrite(max(((x + c0)/c1)*c1, x), b, c1 > 0 && c0 <= 0) ||
               rewrite(max(x, ((x + c0)/c1)*c1), a, c1 > 0 && c0 <= 0))))) {
            return rewrite.result;
        }

        if (EVAL_IN_LAMBDA
            (rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1)))) ||
             rewrite(max(max(x, c0), y), max(max(x, y), c0)) ||
             rewrite(max(max(x, y), max(x, z)), max(max(y, z), x)) ||
             rewrite(max(max(y, x), max(x, z)), max(max(y, z), x)) ||
             rewrite(max(max(x, y), max(z, x)), max(max(y, z), x)) ||
             rewrite(max(max(y, x), max(z, x)), max(max(y, z), x)) ||
             rewrite(max(max(x, y), max(z, w)), max(max(max(x, y), z), w)) ||
             rewrite(max(broadcast(x), broadcast(y)), broadcast(max(x, y), lanes)) ||
             rewrite(max(broadcast(x), ramp(y, z)), max(b, a)) ||
             rewrite(max(max(x, broadcast(y)), broadcast(z)), max(x, broadcast(max(y, z), lanes))) ||
             rewrite(max(min(x, y), min(x, z)), min(x, max(y, z))) ||
             rewrite(max(min(x, y), min(z, x)), min(x, max(y, z))) ||
             rewrite(max(min(y, x), min(x, z)), min(max(y, z), x)) ||
             rewrite(max(min(y, x), min(z, x)), min(max(y, z), x)) ||
             rewrite(max(min(max(x, y), z), y), max(min(x, z), y)) ||
             rewrite(max(min(max(y, x), z), y), max(y, min(x, z))) ||
             rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1)))) ||

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

               rewrite(max(x, x - y), x - min(0, y)) ||
               rewrite(max(x - y, x), x - min(0, y)) ||
               rewrite(max(x, (x - y) + z), x + max(0, z - y)) ||
               rewrite(max(x, z + (x - y)), x + max(0, z - y)) ||
               rewrite(max(x, (x - y) - z), x - min(0, y + z)) ||
               rewrite(max((x - y) + z, x), max(0, z - y) + x) ||
               rewrite(max(z + (x - y), x), max(0, z - y) + x) ||
               rewrite(max((x - y) - z, x), x - min(0, y + z)) ||

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

               rewrite(max(select(x, y, z), select(x, w, u)), select(x, max(y, w), max(z, u))) ||

               rewrite(max(c0 - x, c1), c0 - min(x, fold(c0 - c1))))) ||
             (no_overflow_int(op->type) &&
              (

               // Synthesized
               #if USE_SYNTHESIZED_RULES
               rewrite(max(x, (min((y - (z + w)), u) + z)), max(min((y - w), (z + u)), x)) ||
               rewrite(max(min(x, (y + z)), (min(w, z) + y)), min(max((y + w), x), (y + z))) ||

               rewrite(max(((x - y) + z), (x + w)), (max((z - y), w) + x)) ||

               // From Google data
               rewrite(max((select((x < y), z, w)*x), (select((x < y), w, z)*x)), max((x*z), (w*x))) ||
               rewrite(max(max((x*x), (x*y)), (y*y)), max((y*y), (x*x))) ||

               #endif

               false)))) {

            return mutate(std::move(rewrite.result), bounds);
        }
    }

    const Shuffle *shuffle_a = a.as<Shuffle>();
    const Shuffle *shuffle_b = b.as<Shuffle>();
    if (shuffle_a && shuffle_b &&
        shuffle_a->is_slice() &&
        shuffle_b->is_slice()) {
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return hoist_slice_vector<Max>(op);
        } else {
            return hoist_slice_vector<Max>(Max::make(a, b));
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Max::make(a, b);
    }
}

}
}
