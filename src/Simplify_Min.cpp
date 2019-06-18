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
        return a;
    }
    if (b_bounds.max_defined && a_bounds.min_defined && b_bounds.max <= a_bounds.min) {
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

        if (EVAL_IN_LAMBDA
            (rewrite(min(x, x), x) ||
             rewrite(min(c0, c1), fold(min(c0, c1))) ||
             rewrite(min(IRMatcher::Indeterminate(), x), a) ||
             rewrite(min(x, IRMatcher::Indeterminate()), b) ||
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

             rewrite(min(intrin(Call::likely, x), x), a) ||
             rewrite(min(x, intrin(Call::likely, x)), b) ||
             rewrite(min(intrin(Call::likely_if_innermost, x), x), a) ||
             rewrite(min(x, intrin(Call::likely_if_innermost, x)), b) ||

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

        if (EVAL_IN_LAMBDA
            (rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1)))) ||
             rewrite(min(min(x, c0), y), min(min(x, y), c0)) ||
             rewrite(min(min(x, y), min(x, z)), min(min(y, z), x)) ||
             rewrite(min(min(y, x), min(x, z)), min(min(y, z), x)) ||
             rewrite(min(min(x, y), min(z, x)), min(min(y, z), x)) ||
             rewrite(min(min(y, x), min(z, x)), min(min(y, z), x)) ||
             rewrite(min(min(x, y), min(z, w)), min(min(min(x, y), z), w)) ||
             rewrite(min(broadcast(x), broadcast(y)), broadcast(min(x, y), lanes)) ||
             rewrite(min(broadcast(x), ramp(y, z)), min(b, a)) ||
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

               rewrite(min(x, x - y), x - max(0, y)) ||
               rewrite(min(x - y, x), x - max(0, y)) ||
               rewrite(min(x, (x - y) + z), x + min(0, z - y)) ||
               rewrite(min(x, z + (x - y)), x + min(0, z - y)) ||
               rewrite(min(x, (x - y) - z), x - max(0, y + z)) ||
               rewrite(min((x - y) + z, x), min(0, z - y) + x) ||
               rewrite(min(z + (x - y), x), min(0, z - y) + x) ||
               rewrite(min((x - y) - z, x), x - max(0, y + z)) ||

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

               rewrite(min(c0 - x, c1), c0 - max(x, fold(c0 - c1))))) ||
              (no_overflow_int(op->type) &&
               (

               // Synthesized
               #if USE_SYNTHESIZED_RULES
               rewrite(min((x + (y + z)), ((z + w) + u)), (min((w + u), (x + y)) + z)) ||
               rewrite(min(((x + (y*z)) - (w*z)), u), min((((y - w)*z) + x), u)) ||
               rewrite(min((min(x, (y + z)) - (w + y)), u), min((min((x - y), z) - w), u)) ||
               rewrite(min(min(x, ((y + z) + w)), (u + y)), min((min((z + w), u) + y), x)) ||
               rewrite(min(min((x + (y + z)), w), (u + z)), min((min((x + y), u) + z), w)) ||
               rewrite(min(min(select((x < y), max(x, z), w), w), x), min(x, w)) ||
               rewrite(min(select((x < y), (z + w), w), w), (min(select((x < y), z, 0), 0) + w)) ||

               rewrite(min((x + (y + z)), (z + w)), (min((x + y), w) + z)) ||
               rewrite(min(((x + y) + z), ((y + w) + u)), (min((w + u), (x + z)) + y)) ||
               rewrite(min((((x + y) + z) + w), (y + u)), (min(((x + z) + w), u) + y)) ||
               rewrite(min((((x + y) - z) + w), (y + u)), (min(((w - z) + x), u) + y)) ||
               rewrite(min(((x + y)*z), ((y*z) + w)), (min((x*z), w) + (y*z))) ||
               rewrite(min(min((x + y), z), ((x + w) + u)), min((min((w + u), y) + x), z)) ||

               rewrite(min((x - y), ((z - y) + w)), (min((w + z), x) - y)) ||
               rewrite(min(min(max(x, y), z), y), min(y, z)) ||

               rewrite(min(x, (max(y, c0) + min(x, z))), min((max(y, c0) + z), x), (0 <= c0)) ||
               rewrite(min((x + (y + z)), (w + z)), (min((x + y), w) + z)) ||
               rewrite(min(min(x, (y + c0)), y), min(x, y), (0 <= c0)) ||
               rewrite(min(min(x, (y + c0)), (min(z, y) + c1)), min((min(y, z) + c1), x), (c1 <= c0)) ||
               rewrite(min(x, (min((x + c0), y) + c1)), min((y + c1), x), (0 <= (c0 + c1))) ||

               rewrite(min((x + y), (((z + y) + w) + u)), (min(((u + z) + w), x) + y)) ||
               rewrite(min(((x + y) + z), ((w + y) + u)), (min((x + z), (u + w)) + y)) ||
               rewrite(min((((x + y) + z) + w), (u + y)), (min(((x + z) + w), u) + y)) ||
               rewrite(min((((x + y) - z) + c0), (w + y)), (min(((x - z) + c0), w) + y)) || // Could be more general
               rewrite(min(min((x + y), z), ((w + x) + u)), min((min((u + w), y) + x), z)) ||
               rewrite(min(min((x + y), z), ((w + y) + c0)), min((min((w + c0), x) + y), z)) || // Could be more general

               rewrite(min((((x + c0)/c2)*c2), (x + c3)), (x + c3), (((0 < c2) && (c2 < 16)) && (((c2 + c3) + -1) <= c0))) ||

               rewrite(min(((x + y)*c0), ((y*c0) - z)), ((y*c0) - max((x*fold((0 - c0))), z))) ||

               // From Google data
               rewrite(min((x - (y + z)), ((x - (w + z)) + u)), ((x - z) - max((w - u), y))) ||
               rewrite(min((select((x < y), z, w)*x), (select((x < y), w, z)*x)), min((x*z), (w*x))) ||

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

}
}
