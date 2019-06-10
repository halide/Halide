#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Sub *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds && no_overflow_int(op->type)) {
        // Doesn't account for correlated a, b, so any
        // cancellation rule that exploits that should always
        // remutate to recalculate the bounds.
        bounds->min_defined = a_bounds.min_defined && b_bounds.max_defined;
        bounds->max_defined = a_bounds.max_defined && b_bounds.min_defined;
        bounds->min = a_bounds.min - b_bounds.max;
        bounds->max = a_bounds.max - b_bounds.min;
        bounds->alignment = a_bounds.alignment - b_bounds.alignment;
        bounds->trim_bounds_using_alignment();
    }

    if (may_simplify(op->type)) {

        auto rewrite = IRMatcher::rewriter(IRMatcher::sub(a, b), op->type);
        const int lanes = op->type.lanes();

        if (rewrite(c0 - c1, fold(c0 - c1)) ||
            rewrite(IRMatcher::Indeterminate() - x, a) ||
            rewrite(x - IRMatcher::Indeterminate(), b) ||
            rewrite(IRMatcher::Overflow() - x, a) ||
            rewrite(x - IRMatcher::Overflow(), b) ||
            rewrite(x - 0, x)) {
            return rewrite.result;
        }

        if (EVAL_IN_LAMBDA
            ((!op->type.is_uint() && rewrite(x - c0, x + fold(-c0), !overflows(-c0))) ||
             rewrite(x - x, 0) || // We want to remutate this just to get better bounds
             rewrite(ramp(x, y) - ramp(z, w), ramp(x - z, y - w, lanes)) ||
             rewrite(ramp(x, y) - broadcast(z), ramp(x - z, y, lanes)) ||
             rewrite(broadcast(x) - ramp(z, w), ramp(x - z, -w, lanes)) ||
             rewrite(broadcast(x) - broadcast(y), broadcast(x - y, lanes)) ||
             rewrite(select(x, y, z) - select(x, w, u), select(x, y - w, z - u)) ||
             rewrite(select(x, y, z) - y, select(x, 0, z - y)) ||
             rewrite(select(x, y, z) - z, select(x, y - z, 0)) ||
             rewrite(y - select(x, y, z), select(x, 0, y - z)) ||
             rewrite(z - select(x, y, z), select(x, z - y, 0)) ||
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
             rewrite((x + y) - (x + z), y - z) ||
             rewrite((x + y) - (z + x), y - z) ||
             rewrite((y + x) - (x + z), y - z) ||
             rewrite((y + x) - (z + x), y - z) ||
             rewrite(((x + y) + z) - x, y + z) ||
             rewrite(((y + x) + z) - x, y + z) ||
             rewrite((z + (x + y)) - x, z + y) ||
             rewrite((z + (y + x)) - x, z + y) ||
             (no_overflow(op->type) &&
              (rewrite(max(x, y) - x, max(0, y - x)) ||
               rewrite(min(x, y) - x, min(0, y - x)) ||
               rewrite(max(x, y) - y, max(x - y, 0)) ||
               rewrite(min(x, y) - y, min(x - y, 0)) ||
               rewrite(x - max(x, y), min(0, x - y), !is_const(x)) ||
               rewrite(x - min(x, y), max(0, x - y), !is_const(x)) ||
               rewrite(y - max(x, y), min(y - x, 0), !is_const(y)) ||
               rewrite(y - min(x, y), max(y - x, 0), !is_const(y)) ||
               rewrite(x*y - x, x*(y - 1)) ||
               rewrite(x*y - y, (x - 1)*y) ||
               rewrite(x - x*y, x*(1 - y)) ||
               rewrite(x - y*x, (1 - y)*x) ||
               rewrite(x - min(x + y, z), max(-y, x - z)) ||
               rewrite(x - min(y + x, z), max(-y, x - z)) ||
               rewrite(x - min(z, x + y), max(x - z, -y)) ||
               rewrite(x - min(z, y + x), max(x - z, -y)) ||
               rewrite(min(x + y, z) - x, min(y, z - x)) ||
               rewrite(min(y + x, z) - x, min(y, z - x)) ||
               rewrite(min(z, x + y) - x, min(z - x, y)) ||
               rewrite(min(z, y + x) - x, min(z - x, y)) ||
               rewrite(min(x, y) - min(y, x), 0) ||
               rewrite(min(x, y) - min(z, w), y - w, can_prove(x - y == z - w, this)) ||
               rewrite(min(x, y) - min(w, z), y - w, can_prove(x - y == z - w, this)) ||

               rewrite(x - max(x + y, z), min(-y, x - z)) ||
               rewrite(x - max(y + x, z), min(-y, x - z)) ||
               rewrite(x - max(z, x + y), min(x - z, -y)) ||
               rewrite(x - max(z, y + x), min(x - z, -y)) ||
               rewrite(max(x + y, z) - x, max(y, z - x)) ||
               rewrite(max(y + x, z) - x, max(y, z - x)) ||
               rewrite(max(z, x + y) - x, max(z - x, y)) ||
               rewrite(max(z, y + x) - x, max(z - x, y)) ||
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
               rewrite(min(x, y) - min(x, w), min(0, y - min(x, w)), can_prove(y <= w, this)) ||
               rewrite(min(x, y) - min(x, w), max(0, min(x, y) - w), can_prove(y >= w, this)) ||
               rewrite(min(x + c0, y) - min(x, w), min(c0, y - min(x, w)), can_prove(y <= w + c0, this)) ||
               rewrite(min(x + c0, y) - min(x, w), max(c0, min(x + c0, y) - w), can_prove(y >= w + c0, this)) ||
               rewrite(min(x, y) - min(x + c1, w), min(fold(-c1), y - min(x + c1, w)), can_prove(y + c1 <= w, this)) ||
               rewrite(min(x, y) - min(x + c1, w), max(fold(-c1), min(x, y) - w), can_prove(y + c1 >= w, this)) ||
               rewrite(min(x + c0, y) - min(x + c1, w), min(fold(c0 - c1), y - min(x + c1, w)), can_prove(y + c1 <= w + c0, this)) ||
               rewrite(min(x + c0, y) - min(x + c1, w), max(fold(c0 - c1), min(x + c0, y) - w), can_prove(y + c1 >= w + c0, this)) ||

               rewrite(min(y, x) - min(w, x), min(0, y - min(x, w)), can_prove(y <= w, this)) ||
               rewrite(min(y, x) - min(w, x), max(0, min(x, y) - w), can_prove(y >= w, this)) ||
               rewrite(min(y, x + c0) - min(w, x), min(c0, y - min(x, w)), can_prove(y <= w + c0, this)) ||
               rewrite(min(y, x + c0) - min(w, x), max(c0, min(x + c0, y) - w), can_prove(y >= w + c0, this)) ||
               rewrite(min(y, x) - min(w, x + c1), min(fold(-c1), y - min(x + c1, w)), can_prove(y + c1 <= w, this)) ||
               rewrite(min(y, x) - min(w, x + c1), max(fold(-c1), min(x, y) - w), can_prove(y + c1 >= w, this)) ||
               rewrite(min(y, x + c0) - min(w, x + c1), min(fold(c0 - c1), y - min(x + c1, w)), can_prove(y + c1 <= w + c0, this)) ||
               rewrite(min(y, x + c0) - min(w, x + c1), max(fold(c0 - c1), min(x + c0, y) - w), can_prove(y + c1 >= w + c0, this)) ||

               rewrite(min(x, y) - min(w, x), min(0, y - min(x, w)), can_prove(y <= w, this)) ||
               rewrite(min(x, y) - min(w, x), max(0, min(x, y) - w), can_prove(y >= w, this)) ||
               rewrite(min(x + c0, y) - min(w, x), min(c0, y - min(x, w)), can_prove(y <= w + c0, this)) ||
               rewrite(min(x + c0, y) - min(w, x), max(c0, min(x + c0, y) - w), can_prove(y >= w + c0, this)) ||
               rewrite(min(x, y) - min(w, x + c1), min(fold(-c1), y - min(x + c1, w)), can_prove(y + c1 <= w, this)) ||
               rewrite(min(x, y) - min(w, x + c1), max(fold(-c1), min(x, y) - w), can_prove(y + c1 >= w, this)) ||
               rewrite(min(x + c0, y) - min(w, x + c1), min(fold(c0 - c1), y - min(x + c1, w)), can_prove(y + c1 <= w + c0, this)) ||
               rewrite(min(x + c0, y) - min(w, x + c1), max(fold(c0 - c1), min(x + c0, y) - w), can_prove(y + c1 >= w + c0, this)) ||

               rewrite(min(y, x) - min(x, w), min(0, y - min(x, w)), can_prove(y <= w, this)) ||
               rewrite(min(y, x) - min(x, w), max(0, min(x, y) - w), can_prove(y >= w, this)) ||
               rewrite(min(y, x + c0) - min(x, w), min(c0, y - min(x, w)), can_prove(y <= w + c0, this)) ||
               rewrite(min(y, x + c0) - min(x, w), max(c0, min(x + c0, y) - w), can_prove(y >= w + c0, this)) ||
               rewrite(min(y, x) - min(x + c1, w), min(fold(-c1), y - min(x + c1, w)), can_prove(y + c1 <= w, this)) ||
               rewrite(min(y, x) - min(x + c1, w), max(fold(-c1), min(x, y) - w), can_prove(y + c1 >= w, this)) ||
               rewrite(min(y, x + c0) - min(x + c1, w), min(fold(c0 - c1), y - min(x + c1, w)), can_prove(y + c1 <= w + c0, this)) ||
               rewrite(min(y, x + c0) - min(x + c1, w), max(fold(c0 - c1), min(x + c0, y) - w), can_prove(y + c1 >= w + c0, this)) ||

               // The equivalent rules for max are what you'd
               // expect. Just swap < and > and min and max (apply the
               // isomorphism x -> -x).
               rewrite(max(x, y) - max(x, w), max(0, y - max(x, w)), can_prove(y >= w, this)) ||
               rewrite(max(x, y) - max(x, w), min(0, max(x, y) - w), can_prove(y <= w, this)) ||
               rewrite(max(x + c0, y) - max(x, w), max(c0, y - max(x, w)), can_prove(y >= w + c0, this)) ||
               rewrite(max(x + c0, y) - max(x, w), min(c0, max(x + c0, y) - w), can_prove(y <= w + c0, this)) ||
               rewrite(max(x, y) - max(x + c1, w), max(fold(-c1), y - max(x + c1, w)), can_prove(y + c1 >= w, this)) ||
               rewrite(max(x, y) - max(x + c1, w), min(fold(-c1), max(x, y) - w), can_prove(y + c1 <= w, this)) ||
               rewrite(max(x + c0, y) - max(x + c1, w), max(fold(c0 - c1), y - max(x + c1, w)), can_prove(y + c1 >= w + c0, this)) ||
               rewrite(max(x + c0, y) - max(x + c1, w), min(fold(c0 - c1), max(x + c0, y) - w), can_prove(y + c1 <= w + c0, this)) ||

               rewrite(max(y, x) - max(w, x), max(0, y - max(x, w)), can_prove(y >= w, this)) ||
               rewrite(max(y, x) - max(w, x), min(0, max(x, y) - w), can_prove(y <= w, this)) ||
               rewrite(max(y, x + c0) - max(w, x), max(c0, y - max(x, w)), can_prove(y >= w + c0, this)) ||
               rewrite(max(y, x + c0) - max(w, x), min(c0, max(x + c0, y) - w), can_prove(y <= w + c0, this)) ||
               rewrite(max(y, x) - max(w, x + c1), max(fold(-c1), y - max(x + c1, w)), can_prove(y + c1 >= w, this)) ||
               rewrite(max(y, x) - max(w, x + c1), min(fold(-c1), max(x, y) - w), can_prove(y + c1 <= w, this)) ||
               rewrite(max(y, x + c0) - max(w, x + c1), max(fold(c0 - c1), y - max(x + c1, w)), can_prove(y + c1 >= w + c0, this)) ||
               rewrite(max(y, x + c0) - max(w, x + c1), min(fold(c0 - c1), max(x + c0, y) - w), can_prove(y + c1 <= w + c0, this)) ||

               rewrite(max(x, y) - max(w, x), max(0, y - max(x, w)), can_prove(y >= w, this)) ||
               rewrite(max(x, y) - max(w, x), min(0, max(x, y) - w), can_prove(y <= w, this)) ||
               rewrite(max(x + c0, y) - max(w, x), max(c0, y - max(x, w)), can_prove(y >= w + c0, this)) ||
               rewrite(max(x + c0, y) - max(w, x), min(c0, max(x + c0, y) - w), can_prove(y <= w + c0, this)) ||
               rewrite(max(x, y) - max(w, x + c1), max(fold(-c1), y - max(x + c1, w)), can_prove(y + c1 >= w, this)) ||
               rewrite(max(x, y) - max(w, x + c1), min(fold(-c1), max(x, y) - w), can_prove(y + c1 <= w, this)) ||
               rewrite(max(x + c0, y) - max(w, x + c1), max(fold(c0 - c1), y - max(x + c1, w)), can_prove(y + c1 >= w + c0, this)) ||
               rewrite(max(x + c0, y) - max(w, x + c1), min(fold(c0 - c1), max(x + c0, y) - w), can_prove(y + c1 <= w + c0, this)) ||

               rewrite(max(y, x) - max(x, w), max(0, y - max(x, w)), can_prove(y >= w, this)) ||
               rewrite(max(y, x) - max(x, w), min(0, max(x, y) - w), can_prove(y <= w, this)) ||
               rewrite(max(y, x + c0) - max(x, w), max(c0, y - max(x, w)), can_prove(y >= w + c0, this)) ||
               rewrite(max(y, x + c0) - max(x, w), min(c0, max(x + c0, y) - w), can_prove(y <= w + c0, this)) ||
               rewrite(max(y, x) - max(x + c1, w), max(fold(-c1), y - max(x + c1, w)), can_prove(y + c1 >= w, this)) ||
               rewrite(max(y, x) - max(x + c1, w), min(fold(-c1), max(x, y) - w), can_prove(y + c1 <= w, this)) ||
               rewrite(max(y, x + c0) - max(x + c1, w), max(fold(c0 - c1), y - max(x + c1, w)), can_prove(y + c1 >= w + c0, this)) ||
               rewrite(max(y, x + c0) - max(x + c1, w), min(fold(c0 - c1), max(x + c0, y) - w), can_prove(y + c1 <= w + c0, this)))) ||

             (no_overflow_int(op->type) &&
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

               // Synthesized
               #if USE_SYNTHESIZED_RULES
               rewrite(((x + (y*z)) - (w*z)), (x - ((w - y)*z))) ||
               rewrite(((min((x - y), z) + (w + y)) - u), (min((y + z), x) + (w - u))) ||
               rewrite((min(x, (y + z)) - (w + y)), (min((x - y), z) - w)) ||

               rewrite((((x*y) + z) - (y*w)), (z - ((w - x)*y))) ||
               rewrite((min((x + y), z) - (y + w)), (min((z - y), x) - w)) ||

               rewrite((0 - (x*c1)), (x*fold((0 - c1)))) ||
               rewrite(((x + y) - min((z + y), w)), (x - min((w - y), z))) ||
               rewrite((((x*y) + z) - (w*y)), (z - ((w - x)*y))) ||
               rewrite((((x*y) + z) - (x*w)), (z - ((w - y)*x))) ||
               rewrite(((x*y) - ((z*y) + w)), (((x - z)*y) - w)) ||
               rewrite((min(((x + y) + z), w) - x), min((w - x), (y + z))) ||
               rewrite((min(((x + y) + z), w) - y), min((w - y), (x + z))) ||
               rewrite((min((min(x, c0) + c1), y) - x), min((min(y, fold((c0 + c1))) - x), c1)) ||
               rewrite((max(((x + y) + z), w) - x), max((w - x), (y + z))) ||
               rewrite((max(((x + y) + z), w) - y), max((w - y), (x + z))) ||
               rewrite((max(max((x + y), z), w) - x), max((max(w, z) - x), y)) ||

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
            return hoist_slice_vector<Sub>(op);
        } else {
            return hoist_slice_vector<Sub>(Sub::make(a, b));
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Sub::make(a, b);
    }
}


}
}
