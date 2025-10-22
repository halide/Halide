#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Sub *op, ExprInfo *info) {
    ExprInfo a_info, b_info;
    Expr a = mutate(op->a, &a_info);
    Expr b = mutate(op->b, &b_info);

    if (info) {
        // Doesn't account for correlated a, b, so any
        // cancellation rule that exploits that should always
        // remutate to recalculate the bounds.
        info->bounds = a_info.bounds - b_info.bounds;
        info->alignment = a_info.alignment - b_info.alignment;
        info->cast_to(op->type);
        info->trim_bounds_using_alignment();
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::sub(a, b), op->type);

    if (rewrite(IRMatcher::Overflow() - x, a) ||
        rewrite(x - IRMatcher::Overflow(), b) ||
        rewrite(x - 0, x)) {
        return rewrite.result;
    }

    // clang-format off
    if (EVAL_IN_LAMBDA
        (rewrite(c0 - c1, fold(c0 - c1)) ||
         (!op->type.is_uint() && rewrite(x - c0, x + fold(-c0), !overflows(-c0))) ||
         rewrite(x - x, 0) || // We want to remutate this just to get better bounds

         rewrite(ramp(x, y, c0) - ramp(z, w, c0), ramp(x - z, y - w, c0)) ||
         rewrite(ramp(x, y, c0) - broadcast(z, c0), ramp(x - z, y, c0)) ||
         rewrite(broadcast(x, c0) - ramp(z, w, c0), ramp(x - z, -w, c0)) ||
         rewrite(broadcast(x, c0) - broadcast(y, c0), broadcast(x - y, c0)) ||
         rewrite(broadcast(x, c0) - broadcast(y, c1), broadcast(x - broadcast(y, fold(c1/c0)), c0), c1 % c0 == 0) ||
         rewrite(broadcast(y, c1) - broadcast(x, c0), broadcast(broadcast(y, fold(c1/c0)) - x, c0), c1 % c0 == 0) ||
         rewrite((x - broadcast(y, c0)) - broadcast(z, c0), x - broadcast(y + z, c0)) ||
         rewrite((x + broadcast(y, c0)) - broadcast(z, c0), x + broadcast(y - z, c0)) ||

         rewrite(ramp(broadcast(x, c0), y, c1) - broadcast(z, c2), ramp(broadcast(x - z, c0), y, c1), c2 == c0 * c1) ||
         rewrite(ramp(ramp(x, y, c0), z, c1) - broadcast(w, c2), ramp(ramp(x - w, y, c0), z, c1), c2 == c0 * c1) ||
         rewrite(select(x, y, z) - select(x, w, u), select(x, y - w, z - u)) ||
         rewrite(select(x, y, z) - y, select(x, 0, z - y)) ||
         rewrite(select(x, y, z) - z, select(x, y - z, 0)) ||
         rewrite(y - select(x, y, z), select(x, 0, y - z)) ||
         rewrite(z - select(x, y, z), select(x, z - y, 0)) ||

         rewrite(select(x, y + w, z) - y, select(x, w, z - y)) ||
         rewrite(select(x, w + y, z) - y, select(x, w, z - y)) ||
         rewrite(select(x, y, z + w) - z, select(x, y - z, w)) ||
         rewrite(select(x, y, w + z) - z, select(x, y - z, w)) ||
         rewrite(select(x, y + (z + w), u) - w, select(x, y + z, u - w)) ||
         rewrite(select(x, y + (z + w), u) - z, select(x, y + w, u - z)) ||
         rewrite(select(x, (y + z) + w, u) - y, select(x, w + z, u - y)) ||
         rewrite(select(x, (y + z) + w, u) - z, select(x, w + y, u - z)) ||
         rewrite(select(x, y + z, w) - (u + y), select(x, z, w - y) - u) ||
         rewrite(select(x, y + z, w) - (u + z), select(x, y, w - z) - u) ||
         rewrite(select(x, y + z, w) - (y + u), select(x, z, w - y) - u) ||
         rewrite(select(x, y + z, w) - (z + u), select(x, y, w - z) - u) ||
         rewrite(y - select(x, y + w, z), 0 - select(x, w, z - y)) ||
         rewrite(y - select(x, w + y, z), 0 - select(x, w, z - y)) ||
         rewrite(z - select(x, y, z + w), 0 - select(x, y - z, w)) ||
         rewrite(z - select(x, y, w + z), 0 - select(x, y - z, w)) ||

         rewrite((x + y) - x, y) ||
         rewrite((x + y) - y, x) ||
         rewrite(x - (x + y), -y) ||
         rewrite(y - (x + y), -x) ||
         rewrite((x - y) - x, -y) ||
         rewrite((select(x, y, z) + w) - select(x, u, v), select(x, y - u, z - v) + w) ||
         rewrite((w + select(x, y, z)) - select(x, u, v), select(x, y - u, z - v) + w) ||
         rewrite(select(x, y, z) - (select(x, u, v) + w), select(x, y - u, z - v) - w) ||
         rewrite(select(x, y, z) - (w + select(x, u, v)), select(x, y - u, z - v) - w) ||
         rewrite((select(x, y, z) - w) - select(x, u, v), select(x, y - u, z - v) - w) ||
         rewrite(c0 - select(x, c1, c2), select(x, fold(c0 - c1), fold(c0 - c2))) ||
         rewrite((x + c0) - c1, x + fold(c0 - c1)) ||
         rewrite((x + c0) - (c1 - y), (x + y) + fold(c0 - c1)) ||
         rewrite((x + c0) - (y + c1), (x - y) + fold(c0 - c1)) ||
         rewrite((x + c0) - y, (x - y) + c0) ||
         rewrite((c0 - x) - (c1 - y), (y - x) + fold(c0 - c1)) ||
         rewrite((c0 - x) - (y + c1), fold(c0 - c1) - (x + y)) ||
         rewrite(x - (y - z), x + (z - y)) ||
         rewrite(x - y*c0, x + y*fold(-c0), c0 < 0 && -c0 > 0) ||
         rewrite(x - (y + c0), (x - y) - c0) ||
         rewrite((c0 - x) - c1, fold(c0 - c1) - x) ||
         rewrite(x*y - z*y, (x - z)*y) ||
         rewrite(x*y - y*z, (x - z)*y) ||
         rewrite(y*x - z*y, y*(x - z)) ||
         rewrite(y*x - y*z, y*(x - z)) ||
         rewrite((u + x*y) - z*y, u + (x - z)*y) ||
         rewrite((u + x*y) - y*z, u + (x - z)*y) ||
         rewrite((u + y*x) - z*y, u + y*(x - z)) ||
         rewrite((u + y*x) - y*z, u + y*(x - z)) ||
         rewrite((u - x*y) - z*y, u - (x + z)*y) ||
         rewrite((u - x*y) - y*z, u - (x + z)*y) ||
         rewrite((u - y*x) - z*y, u - y*(x + z)) ||
         rewrite((u - y*x) - y*z, u - y*(x + z)) ||
         rewrite((x*y + u) - z*y, u + (x - z)*y) ||
         rewrite((x*y + u) - y*z, u + (x - z)*y) ||
         rewrite((y*x + u) - z*y, u + y*(x - z)) ||
         rewrite((y*x + u) - y*z, u + y*(x - z)) ||
         rewrite((x*y - u) - z*y, (x - z)*y - u) ||
         rewrite((x*y - u) - y*z, (x - z)*y - u) ||
         rewrite((y*x - u) - z*y, y*(x - z) - u) ||
         rewrite((y*x - u) - y*z, y*(x - z) - u) ||
         rewrite(x*y - (u + z*y), (x - z)*y - u) ||
         rewrite(x*y - (u + y*z), (x - z)*y - u) ||
         rewrite(y*x - (u + z*y), y*(x - z) - u) ||
         rewrite(y*x - (u + y*z), y*(x - z) - u) ||
         rewrite(x*y - (u - z*y), (x + z)*y - u) ||
         rewrite(x*y - (u - y*z), (x + z)*y - u) ||
         rewrite(y*x - (u - z*y), y*(x + z) - u) ||
         rewrite(y*x - (u - y*z), y*(x + z) - u) ||
         rewrite(x*y - (z*y + u), (x - z)*y - u) ||
         rewrite(x*y - (y*z + u), (x - z)*y - u) ||
         rewrite(y*x - (z*y + u), y*(x - z) - u) ||
         rewrite(y*x - (y*z + u), y*(x - z) - u) ||
         rewrite(x*y - (z*y - u), (x - z)*y + u) ||
         rewrite(x*y - (y*z - u), (x - z)*y + u) ||
         rewrite(y*x - (z*y - u), y*(x - z) + u) ||
         rewrite(y*x - (y*z - u), y*(x - z) + u) ||
         rewrite((x + y) - (x + z), y - z) ||
         rewrite((x + y) - (z + x), y - z) ||
         rewrite((y + x) - (x + z), y - z) ||
         rewrite((y + x) - (z + x), y - z) ||
         rewrite(((x + y) + z) - x, y + z) ||
         rewrite(((y + x) + z) - x, y + z) ||
         rewrite((z + (x + y)) - x, z + y) ||
         rewrite((z + (y + x)) - x, z + y) ||

         rewrite(x - (y + (x - z)), z - y) ||
         rewrite(x - ((x - y) + z), y - z) ||
         rewrite((x + (y - z)) - y, x - z) ||
         rewrite(((x - y) + z) - x, z - y) ||

         rewrite(x - (y + (x + z)), 0 - (y + z)) ||
         rewrite(x - (y + (z + x)), 0 - (y + z)) ||
         rewrite(x - ((x + y) + z), 0 - (y + z)) ||
         rewrite(x - ((y + x) + z), 0 - (y + z)) ||
         rewrite((x + y) - (z + (w + x)), y - (z + w)) ||
         rewrite((x + y) - (z + (w + y)), x - (z + w)) ||
         rewrite((x + y) - (z + (x + w)), y - (z + w)) ||
         rewrite((x + y) - (z + (y + w)), x - (z + w)) ||
         rewrite((x + y) - ((x + z) + w), y - (z + w)) ||
         rewrite((x + y) - ((y + z) + w), x - (z + w)) ||
         rewrite((x + y) - ((z + x) + w), y - (z + w)) ||
         rewrite((x + y) - ((z + y) + w), x - (z + w)) ||

         rewrite((x - y) - (x + z), 0 - y - z) ||
         rewrite((x - y) - (z + x), 0 - y - z) ||

         rewrite(((x + y) - z) - x, y - z) ||
         rewrite(((x + y) - z) - y, x - z) ||

         rewrite(x - min(x - y, 0), max(x, y)) ||
         rewrite(x - max(x - y, 0), min(x, y)) ||
         rewrite((x + y) - min(x, y), max(y, x)) ||
         rewrite((x + y) - min(y, x), max(y, x)) ||
         rewrite((x + y) - max(x, y), min(y, x)) ||
         rewrite((x + y) - max(y, x), min(x, y)) ||

         rewrite(0 - (x + (y - z)), z - (x + y)) ||
         rewrite(0 - ((x - y) + z), y - (x + z)) ||
         rewrite(((x - y) - z) - x, 0 - (y + z)) ||

         rewrite(x - x%c0, (x/c0)*c0) ||
         rewrite(x - ((x + c0)/c1)*c1, (x + c0)%c1 - c0, c1 > 0) ||

         // Hoist shuffles. The Shuffle visitor wants to sink
         // extract_elements to the leaves, and those count as degenerate
         // slices, so only hoist shuffles that grab more than one lane.
         rewrite(slice(x, c0, c1, c2) - slice(y, c0, c1, c2), slice(x - y, c0, c1, c2), c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite(slice(x, c0, c1, c2) - (z + slice(y, c0, c1, c2)), slice(x - y, c0, c1, c2) - z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite(slice(x, c0, c1, c2) - (slice(y, c0, c1, c2) + z), slice(x - y, c0, c1, c2) - z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite((slice(x, c0, c1, c2) - z) - slice(y, c0, c1, c2), slice(x - y, c0, c1, c2) - z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite((z - slice(x, c0, c1, c2)) - slice(y, c0, c1, c2), z - slice(x + y, c0, c1, c2), c2 > 1 && lanes_of(x) == lanes_of(y)) ||

         (no_overflow(op->type) && EVAL_IN_LAMBDA
          (rewrite(max(x, y) - x, max(y - x, 0)) ||
           rewrite(min(x, y) - x, min(y - x, 0)) ||
           rewrite(max(x, y) - y, max(x - y, 0)) ||
           rewrite(min(x, y) - y, min(x - y, 0)) ||

           rewrite(x - max(x, y), min(x - y, 0), !is_const(x)) ||
           rewrite(x - min(x, y), max(x - y, 0), !is_const(x)) ||
           rewrite(y - max(x, y), min(y - x, 0), !is_const(y)) ||
           rewrite(y - min(x, y), max(y - x, 0), !is_const(y)) ||

           rewrite(x - min(y, x - z), max(x - y, z)) ||
           rewrite(x - min(x - y, z), max(y, x - z)) ||
           rewrite(x - max(y, x - z), min(x - y, z)) ||
           rewrite(x - max(x - y, z), min(y, x - z)) ||

           rewrite(min(x - y, 0) - x, 0 - max(x, y)) ||
           rewrite(max(x - y, 0) - x, 0 - min(x, y)) ||
           rewrite(min(x, y) - (x + y), 0 - max(y, x)) ||
           rewrite(min(x, y) - (y + x), 0 - max(x, y)) ||
           rewrite(max(x, y) - (x + y), 0 - min(x, y)) ||
           rewrite(max(x, y) - (y + x), 0 - min(y, x)) ||

           // Negate a clamped subtract
           rewrite(z - max(x - y, c0), z + min(y - x, fold(-c0))) ||
           rewrite(z - min(x - y, c0), z + max(y - x, fold(-c0))) ||
           rewrite(z - max(min(x - y, c0), c1), z + min(max(y - x, fold(-c0)), fold(-c1))) ||
           rewrite(z - min(max(x - y, c0), c1), z + max(min(y - x, fold(-c0)), fold(-c1))) ||

           rewrite(x*y - x, x*(y - 1)) ||
           rewrite(x*y - y, (x - 1)*y) ||
           rewrite(x - x*y, x*(1 - y)) ||
           rewrite(x - y*x, (1 - y)*x) ||

           // Cancel a term from one side of a min or max. Some of
           // these rules introduce a new constant zero, so we require
           // that the cancelled term is not a constant. This way
           // there can't be a cycle. For some rules we know by
           // context that the cancelled term is not a constant
           // (e.g. it appears on the LHS of an addition).
           rewrite((x - min(z, (x + y))), (0 - min(z - x, y)), !is_const(x)) ||
           rewrite((x - min(z, (y + x))), (0 - min(z - x, y)), !is_const(x)) ||
           rewrite((x - min((x + y), z)), (0 - min(z - x, y)), !is_const(x)) ||
           rewrite((x - min((y + x), z)), (0 - min(z - x, y)), !is_const(x)) ||
           rewrite((x - min(y, (w + (x + z)))), (0 - min(y - x, w + z)), !is_const(x)) ||
           rewrite((x - min(y, (w + (z + x)))), (0 - min(y - x, z + w)), !is_const(x)) ||
           rewrite((x - min(y, ((x + z) + w))), (0 - min(y - x, z + w)), !is_const(x)) ||
           rewrite((x - min(y, ((z + x) + w))), (0 - min(y - x, z + w)), !is_const(x)) ||
           rewrite((x - min((w + (x + z)), y)), (0 - min(y - x, w + z)), !is_const(x)) ||
           rewrite((x - min((w + (z + x)), y)), (0 - min(y - x, z + w)), !is_const(x)) ||
           rewrite((x - min(((x + z) + w), y)), (0 - min(y - x, w + z)), !is_const(x)) ||
           rewrite((x - min(((z + x) + w), y)), (0 - min(y - x, w + z)), !is_const(x)) ||

           rewrite(min(x + y, z) - x, min(z - x, y)) ||
           rewrite(min(y + x, z) - x, min(z - x, y)) ||
           rewrite(min(z, x + y) - x, min(z - x, y)) ||
           rewrite(min(z, y + x) - x, min(z - x, y)) ||
           rewrite((min(x, (w + (y + z))) - z), min(x - z, w + y)) ||
           rewrite((min(x, (w + (z + y))) - z), min(x - z, w + y)) ||
           rewrite((min(x, ((y + z) + w)) - z), min(x - z, y + w)) ||
           rewrite((min(x, ((z + y) + w)) - z), min(x - z, y + w)) ||
           rewrite((min((w + (y + z)), x) - z), min(x - z, w + y)) ||
           rewrite((min((w + (z + y)), x) - z), min(x - z, w + y)) ||
           rewrite((min(((y + z) + w), x) - z), min(x - z, y + w)) ||
           rewrite((min(((z + y) + w), x) - z), min(x - z, y + w)) ||

           rewrite(min(x, y) - min(y, x), 0) ||
           rewrite(min(x, y) - min(z, w), y - w, can_prove(x - y == z - w, this)) ||
           rewrite(min(x, y) - min(w, z), y - w, can_prove(x - y == z - w, this)) ||
           rewrite(min(x*c0, c1) - min(x, c2)*c0, min(c1 - min(x, c2)*c0, 0), c0 > 0 && c1 <= c2*c0) ||

           rewrite((x - max(z, (x + y))), (0 - max(z - x, y)), !is_const(x)) ||
           rewrite((x - max(z, (y + x))), (0 - max(z - x, y)), !is_const(x)) ||
           rewrite((x - max((x + y), z)), (0 - max(z - x, y)), !is_const(x)) ||
           rewrite((x - max((y + x), z)), (0 - max(z - x, y)), !is_const(x)) ||
           rewrite((x - max(y, (w + (x + z)))), (0 - max(y - x, w + z)), !is_const(x)) ||
           rewrite((x - max(y, (w + (z + x)))), (0 - max(y - x, z + w)), !is_const(x)) ||
           rewrite((x - max(y, ((x + z) + w))), (0 - max(y - x, z + w)), !is_const(x)) ||
           rewrite((x - max(y, ((z + x) + w))), (0 - max(y - x, z + w)), !is_const(x)) ||
           rewrite((x - max((w + (x + z)), y)), (0 - max(y - x, w + z)), !is_const(x)) ||
           rewrite((x - max((w + (z + x)), y)), (0 - max(y - x, z + w)), !is_const(x)) ||
           rewrite((x - max(((x + z) + w), y)), (0 - max(y - x, w + z)), !is_const(x)) ||
           rewrite((x - max(((z + x) + w), y)), (0 - max(y - x, w + z)), !is_const(x)) ||

           rewrite(max(x + y, z) - x, max(z - x, y)) ||
           rewrite(max(y + x, z) - x, max(z - x, y)) ||
           rewrite(max(z, x + y) - x, max(z - x, y)) ||
           rewrite(max(z, y + x) - x, max(z - x, y)) ||
           rewrite((max(x, (w + (y + z))) - z), max(x - z, w + y)) ||
           rewrite((max(x, (w + (z + y))) - z), max(x - z, w + y)) ||
           rewrite((max(x, ((y + z) + w)) - z), max(x - z, y + w)) ||
           rewrite((max(x, ((z + y) + w)) - z), max(x - z, y + w)) ||
           rewrite((max((w + (y + z)), x) - z), max(x - z, w + y)) ||
           rewrite((max((w + (z + y)), x) - z), max(x - z, w + y)) ||
           rewrite((max(((y + z) + w), x) - z), max(x - z, y + w)) ||
           rewrite((max(((z + y) + w), x) - z), max(x - z, y + w)) ||

           rewrite(max(x, y) - max(y, x), 0) ||
           rewrite(max(x, y) - max(z, w), y - w, can_prove(x - y == z - w, this)) ||
           rewrite(max(x, y) - max(w, z), y - w, can_prove(x - y == z - w, this)) ||

           // When you have min(x, y) - min(z, w) and no further
           // information, there are four possible ways for the
           // mins to resolve. However if you can prove that the
           // decisions are correlated (i.e. x < y implies z < w or
           // vice versa), then there are simplifications to be
           // made that tame x. Whether or not these
           // simplifications are profitable depends on what terms
           // end up being constant.

           // If x < y implies z < w:
           //   min(x, y) - min(z, w)
           // = min(x - min(z, w), y - min(z, w))   using the distributive properties of min/max
           // = min(x - z, y - min(z, w))           using the implication
           // This duplicates z, so it's good if x - z causes some cancellation (e.g. they are equal)

           // If, on the other hand, z < w implies x < y:
           //   min(x, y) - min(z, w)
           // = max(min(x, y) - z, min(x, y) - w)   using the distributive properties of min/max
           // = max(x - z, min(x, y) - w)           using the implication
           // Again, this is profitable when x - z causes some cancellation

           // What follows are special cases of this general
           // transformation where it is easy to see that x - z
           // cancels and that there is an implication in one
           // direction or the other.

           // Then the actual rules. We consider only cases where x and z differ by a constant.
           rewrite(min(x, y) - min(x, w), min(y - min(x, w), 0), can_prove(y <= w, this)) ||
           rewrite(min(x, y) - min(x, w), max(min(x, y) - w, 0), can_prove(y >= w, this)) ||
           rewrite(min(x + c0, y) - min(x, w), min(y - min(x, w), c0), can_prove(y <= w + c0, this)) ||
           rewrite(min(x + c0, y) - min(x, w), max(min(x + c0, y) - w, c0), can_prove(y >= w + c0, this)) ||
           rewrite(min(x, y) - min(x + c1, w), min(y - min(x + c1, w), fold(-c1)), can_prove(y + c1 <= w, this)) ||
           rewrite(min(x, y) - min(x + c1, w), max(min(x, y) - w, fold(-c1)), can_prove(y + c1 >= w, this)) ||
           rewrite(min(x + c0, y) - min(x + c1, w), min(y - min(x + c1, w), fold(c0 - c1)), can_prove(y + c1 <= w + c0, this)) ||
           rewrite(min(x + c0, y) - min(x + c1, w), max(min(x + c0, y) - w, fold(c0 - c1)), can_prove(y + c1 >= w + c0, this)) ||

           rewrite(min(y, x) - min(w, x), min(y - min(x, w), 0), can_prove(y <= w, this)) ||
           rewrite(min(y, x) - min(w, x), max(min(x, y) - w, 0), can_prove(y >= w, this)) ||
           rewrite(min(y, x + c0) - min(w, x), min(y - min(x, w), c0), can_prove(y <= w + c0, this)) ||
           rewrite(min(y, x + c0) - min(w, x), max(min(x + c0, y) - w, c0), can_prove(y >= w + c0, this)) ||
           rewrite(min(y, x) - min(w, x + c1), min(y - min(x + c1, w), fold(-c1)), can_prove(y + c1 <= w, this)) ||
           rewrite(min(y, x) - min(w, x + c1), max(min(x, y) - w, fold(-c1)), can_prove(y + c1 >= w, this)) ||
           rewrite(min(y, x + c0) - min(w, x + c1), min(y - min(x + c1, w), fold(c0 - c1)), can_prove(y + c1 <= w + c0, this)) ||
           rewrite(min(y, x + c0) - min(w, x + c1), max(min(x + c0, y) - w, fold(c0 - c1)), can_prove(y + c1 >= w + c0, this)) ||

           rewrite(min(x, y) - min(w, x), min(y - min(x, w), 0), can_prove(y <= w, this)) ||
           rewrite(min(x, y) - min(w, x), max(min(x, y) - w, 0), can_prove(y >= w, this)) ||
           rewrite(min(x + c0, y) - min(w, x), min(y - min(x, w), c0), can_prove(y <= w + c0, this)) ||
           rewrite(min(x + c0, y) - min(w, x), max(min(x + c0, y) - w, c0), can_prove(y >= w + c0, this)) ||
           rewrite(min(x, y) - min(w, x + c1), min(y - min(x + c1, w), fold(-c1)), can_prove(y + c1 <= w, this)) ||
           rewrite(min(x, y) - min(w, x + c1), max(min(x, y) - w, fold(-c1)), can_prove(y + c1 >= w, this)) ||
           rewrite(min(x + c0, y) - min(w, x + c1), min(y - min(x + c1, w), fold(c0 - c1)), can_prove(y + c1 <= w + c0, this)) ||
           rewrite(min(x + c0, y) - min(w, x + c1), max(min(x + c0, y) - w, fold(c0 - c1)), can_prove(y + c1 >= w + c0, this)) ||

           rewrite(min(y, x) - min(x, w), min(y - min(x, w), 0), can_prove(y <= w, this)) ||
           rewrite(min(y, x) - min(x, w), max(min(x, y) - w, 0), can_prove(y >= w, this)) ||
           rewrite(min(y, x + c0) - min(x, w), min(y - min(x, w), c0), can_prove(y <= w + c0, this)) ||
           rewrite(min(y, x + c0) - min(x, w), max(min(x + c0, y) - w, c0), can_prove(y >= w + c0, this)) ||
           rewrite(min(y, x) - min(x + c1, w), min(y - min(x + c1, w), fold(-c1)), can_prove(y + c1 <= w, this)) ||
           rewrite(min(y, x) - min(x + c1, w), max(min(x, y) - w, fold(-c1)), can_prove(y + c1 >= w, this)) ||
           rewrite(min(y, x + c0) - min(x + c1, w), min(y - min(x + c1, w), fold(c0 - c1)), can_prove(y + c1 <= w + c0, this)) ||
           rewrite(min(y, x + c0) - min(x + c1, w), max(min(x + c0, y) - w, fold(c0 - c1)), can_prove(y + c1 >= w + c0, this)) ||

           // The equivalent rules for max are what you'd
           // expect. Just swap < and > and min and max (apply the
           // isomorphism x -> -x).
           rewrite(max(x, y) - max(x, w), max(y - max(x, w), 0), can_prove(y >= w, this)) ||
           rewrite(max(x, y) - max(x, w), min(max(x, y) - w, 0), can_prove(y <= w, this)) ||
           rewrite(max(x + c0, y) - max(x, w), max(y - max(x, w), c0), can_prove(y >= w + c0, this)) ||
           rewrite(max(x + c0, y) - max(x, w), min(max(x + c0, y) - w, c0), can_prove(y <= w + c0, this)) ||
           rewrite(max(x, y) - max(x + c1, w), max(y - max(x + c1, w), fold(-c1)), can_prove(y + c1 >= w, this)) ||
           rewrite(max(x, y) - max(x + c1, w), min(max(x, y) - w, fold(-c1)), can_prove(y + c1 <= w, this)) ||
           rewrite(max(x + c0, y) - max(x + c1, w), max(y - max(x + c1, w), fold(c0 - c1)), can_prove(y + c1 >= w + c0, this)) ||
           rewrite(max(x + c0, y) - max(x + c1, w), min(max(x + c0, y) - w, fold(c0 - c1)), can_prove(y + c1 <= w + c0, this)) ||

           rewrite(max(y, x) - max(w, x), max(y - max(x, w), 0), can_prove(y >= w, this)) ||
           rewrite(max(y, x) - max(w, x), min(max(x, y) - w, 0), can_prove(y <= w, this)) ||
           rewrite(max(y, x + c0) - max(w, x), max(y - max(x, w), c0), can_prove(y >= w + c0, this)) ||
           rewrite(max(y, x + c0) - max(w, x), min(max(x + c0, y) - w, c0), can_prove(y <= w + c0, this)) ||
           rewrite(max(y, x) - max(w, x + c1), max(y - max(x + c1, w), fold(-c1)), can_prove(y + c1 >= w, this)) ||
           rewrite(max(y, x) - max(w, x + c1), min(max(x, y) - w, fold(-c1)), can_prove(y + c1 <= w, this)) ||
           rewrite(max(y, x + c0) - max(w, x + c1), max(y - max(x + c1, w), fold(c0 - c1)), can_prove(y + c1 >= w + c0, this)) ||
           rewrite(max(y, x + c0) - max(w, x + c1), min(max(x + c0, y) - w, fold(c0 - c1)), can_prove(y + c1 <= w + c0, this)) ||

           rewrite(max(x, y) - max(w, x), max(y - max(x, w), 0), can_prove(y >= w, this)) ||
           rewrite(max(x, y) - max(w, x), min(max(x, y) - w, 0), can_prove(y <= w, this)) ||
           rewrite(max(x + c0, y) - max(w, x), max(y - max(x, w), c0), can_prove(y >= w + c0, this)) ||
           rewrite(max(x + c0, y) - max(w, x), min(max(x + c0, y) - w, c0), can_prove(y <= w + c0, this)) ||
           rewrite(max(x, y) - max(w, x + c1), max(y - max(x + c1, w), fold(-c1)), can_prove(y + c1 >= w, this)) ||
           rewrite(max(x, y) - max(w, x + c1), min(max(x, y) - w, fold(-c1)), can_prove(y + c1 <= w, this)) ||
           rewrite(max(x + c0, y) - max(w, x + c1), max(y - max(x + c1, w), fold(c0 - c1)), can_prove(y + c1 >= w + c0, this)) ||
           rewrite(max(x + c0, y) - max(w, x + c1), min(max(x + c0, y) - w, fold(c0 - c1)), can_prove(y + c1 <= w + c0, this)) ||

           rewrite(max(y, x) - max(x, w), max(y - max(x, w), 0), can_prove(y >= w, this)) ||
           rewrite(max(y, x) - max(x, w), min(max(x, y) - w, 0), can_prove(y <= w, this)) ||
           rewrite(max(y, x + c0) - max(x, w), max(y - max(x, w), c0), can_prove(y >= w + c0, this)) ||
           rewrite(max(y, x + c0) - max(x, w), min(max(x + c0, y) - w, c0), can_prove(y <= w + c0, this)) ||
           rewrite(max(y, x) - max(x + c1, w), max(y - max(x + c1, w), fold(-c1)), can_prove(y + c1 >= w, this)) ||
           rewrite(max(y, x) - max(x + c1, w), min(max(x, y) - w, fold(-c1)), can_prove(y + c1 <= w, this)) ||
           rewrite(max(y, x + c0) - max(x + c1, w), max(y - max(x + c1, w), fold(c0 - c1)), can_prove(y + c1 >= w + c0, this)) ||
           rewrite(max(y, x + c0) - max(x + c1, w), min(max(x + c0, y) - w, fold(c0 - c1)), can_prove(y + c1 <= w + c0, this)))) ||

         (no_overflow_int(op->type) && EVAL_IN_LAMBDA
          (rewrite(c0 - (c1 - x)/c2, (fold(c0*c2 - c1 + c2 - 1) + x)/c2, c2 > 0) ||
           rewrite(c0 - (x + c1)/c2, (fold(c0*c2 - c1 + c2 - 1) - x)/c2, c2 > 0) ||
           rewrite(x - (x + y)/c0, (x*fold(c0 - 1) - y + fold(c0 - 1))/c0, c0 > 0) ||
           rewrite(x - (x - y)/c0, (x*fold(c0 - 1) + y + fold(c0 - 1))/c0, c0 > 0) ||
           rewrite(x - (y + x)/c0, (x*fold(c0 - 1) - y + fold(c0 - 1))/c0, c0 > 0) ||
           rewrite(x - (y - x)/c0, (x*fold(c0 + 1) - y + fold(c0 - 1))/c0, c0 > 0) ||
           rewrite((x + y)/c0 - x, (x*fold(1 - c0) + y)/c0) ||
           rewrite((y + x)/c0 - x, (y + x*fold(1 - c0))/c0) ||
           rewrite((x - y)/c0 - x, (x*fold(1 - c0) - y)/c0) ||
           rewrite((y - x)/c0 - x, (y - x*fold(1 + c0))/c0) ||

           rewrite((x/c0)*c0 - x, -(x % c0), c0 > 0) ||
           rewrite(x - (x/c0)*c0, x % c0, c0 > 0) ||
           rewrite(((x + c0)/c1)*c1 - x, (-x) % c1, c1 > 0 && c0 + 1 == c1) ||
           rewrite(x - ((x + c0)/c1)*c1, ((x + c0) % c1) + fold(-c0), c1 > 0 && c0 + 1 == c1) ||
           rewrite(x * c0 - y * c1, (x * fold(c0 / c1) - y) * c1, c0 % c1 == 0) ||
           rewrite(x * c0 - y * c1, (x - y * fold(c1 / c0)) * c0, c1 % c0 == 0) ||
           // Various forms of (x +/- a)/c - (x +/- b)/c. We can
           // *almost* cancel the x.  The right thing to do depends
           // on which of a or b is a constant, and we also need to
           // catch the cases where that constant is zero.
           rewrite(((x + y) + z)/c0 - ((y + x) + w)/c0, ((x + y) + z)/c0 - ((x + y) + w)/c0, c0 > 0) ||
           rewrite((x + y)/c0 - (y + x)/c0, 0, c0 != 0) ||
           rewrite((x + y)/c0 - (x + c1)/c0, (((x + fold(c1 % c0)) % c0) + (y - c1))/c0, c0 > 0) ||
           rewrite((x + c1)/c0 - (x + y)/c0, ((fold(c0 + c1 - 1) - y) - ((x + fold(c1 % c0)) % c0))/c0, c0 > 0) ||
           rewrite((x - y)/c0 - (x + c1)/c0, (((x + fold(c1 % c0)) % c0) - y - c1)/c0, c0 > 0) ||
           rewrite((x + c1)/c0 - (x - y)/c0, ((y + fold(c0 + c1 - 1)) - ((x + fold(c1 % c0)) % c0))/c0, c0 > 0) ||
           rewrite(x/c0 - (x + y)/c0, ((fold(c0 - 1) - y) - (x % c0))/c0, c0 > 0) ||
           rewrite((x + y)/c0 - x/c0, ((x % c0) + y)/c0, c0 > 0) ||
           rewrite(x/c0 - (x - y)/c0, ((y + fold(c0 - 1)) - (x % c0))/c0, c0 > 0) ||
           rewrite((x - y)/c0 - x/c0, ((x % c0) - y)/c0, c0 > 0) ||

           // Simplification of bounds code for various tail
           // strategies requires cancellations of the form:
           // min(f(x), y) - g(x)

           // There are many potential variants of these rules if
           // we start adding commutative/associative rewritings
           // of them, or consider max as well as min. We
           // explicitly only include the ones necessary to get
           // correctness_nested_tail_strategies to pass.
           rewrite((min(x + y, z) + w) - x, min(z - x, y) + w) ||
           rewrite(min((x + y) + w, z) - x, min(z - x, y + w)) ||
           rewrite(min(min(x + z, y), w) - x, min(min(y, w) - x, z)) ||
           rewrite(min(min(y, x + z), w) - x, min(min(y, w) - x, z)) ||

           rewrite(min((x + y)*u + z, w) - x*u, min(w - x*u, y*u + z)) ||
           rewrite(min((y + x)*u + z, w) - x*u, min(w - x*u, y*u + z)) ||

           // Splits can introduce confounding divisions
           rewrite(min(x*c0 + y, z) / c1 - x*c2, min(y, z - x*c0) / c1, c0 == c1 * c2) ||
           rewrite(min(z, x*c0 + y) / c1 - x*c2, min(y, z - x*c0) / c1, c0 == c1 * c2) ||

           // There could also be an addition inside the division (e.g. if it's division rounding up)
           rewrite((min(x*c0 + y, z) + w) / c1 - x*c2, (min(y, z - x*c0) + w) / c1, c0 == c1 * c2) ||
           rewrite((min(z, x*c0 + y) + w) / c1 - x*c2, (min(z - x*c0, y) + w) / c1, c0 == c1 * c2) ||

           false)))) {
        return mutate(rewrite.result, info);
    }
    // clang-format on

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Sub::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
