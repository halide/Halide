#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Div *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds && no_overflow_int(op->type)) {
        bounds->min = INT64_MAX;
        bounds->max = INT64_MIN;

        // Enumerate all possible values for the min and max and take the extreme values.
        if (a_bounds.min_defined && b_bounds.min_defined && b_bounds.min != 0) {
            int64_t v = div_imp(a_bounds.min, b_bounds.min);
            bounds->min = std::min(bounds->min, v);
            bounds->max = std::max(bounds->max, v);
        }

        if (a_bounds.min_defined && b_bounds.max_defined && b_bounds.max != 0) {
            int64_t v = div_imp(a_bounds.min, b_bounds.max);
            bounds->min = std::min(bounds->min, v);
            bounds->max = std::max(bounds->max, v);
        }

        if (a_bounds.max_defined && b_bounds.max_defined && b_bounds.max != 0) {
            int64_t v = div_imp(a_bounds.max, b_bounds.max);
            bounds->min = std::min(bounds->min, v);
            bounds->max = std::max(bounds->max, v);
        }

        if (a_bounds.max_defined && b_bounds.min_defined && b_bounds.min != 0) {
            int64_t v = div_imp(a_bounds.max, b_bounds.min);
            bounds->min = std::min(bounds->min, v);
            bounds->max = std::max(bounds->max, v);
        }

        const bool b_positive = b_bounds.min_defined && b_bounds.min > 0;
        const bool b_negative = b_bounds.max_defined && b_bounds.max < 0;

        if ((b_positive && !b_bounds.max_defined) ||
            (b_negative && !b_bounds.min_defined)) {
            // Take limit as b -> +/- infinity
            int64_t v = 0;
            bounds->min = std::min(bounds->min, v);
            bounds->max = std::max(bounds->max, v);
        }

        bounds->min_defined = ((a_bounds.min_defined && b_positive) ||
                               (a_bounds.max_defined && b_negative));
        bounds->max_defined = ((a_bounds.max_defined && b_positive) ||
                               (a_bounds.min_defined && b_negative));

        // That's as far as we can get knowing the sign of the
        // denominator. For bounded numerators, we additionally know
        // that div can't make anything larger in magnitude, so we can
        // take the intersection with that.
        if (a_bounds.max_defined && a_bounds.min_defined) {
            int64_t v = std::max(a_bounds.max, -a_bounds.min);
            if (bounds->min_defined) {
                bounds->min = std::max(bounds->min, -v);
            } else {
                bounds->min = -v;
            }
            if (bounds->max_defined) {
                bounds->max = std::min(bounds->max, v);
            } else {
                bounds->max = v;
            }
            bounds->min_defined = bounds->max_defined = true;
        }

        // Bounded numerator divided by constantish
        // denominator can sometimes collapse things to a
        // constant at this point
        if (bounds->min_defined &&
            bounds->max_defined &&
            bounds->max == bounds->min) {
            if (op->type.can_represent(bounds->min)) {
                return make_const(op->type, bounds->min);
            } else {
                // Even though this is 'no-overflow-int', if the result
                // we calculate can't fit into the destination type,
                // we're better off returning an overflow condition than
                // a known-wrong value. (Note that no_overflow_int() should
                // only be true for signed integers.)
                internal_assert(op->type.is_int());
                return make_signed_integer_overflow(op->type);
            }
        }
        // Code downstream can use min/max in calculated-but-unused arithmetic
        // that can lead to UB (and thus, flaky failures under ASAN/UBSAN)
        // if we leave them set to INT64_MAX/INT64_MIN; normalize to zero to avoid this.
        if (!bounds->min_defined) bounds->min = 0;
        if (!bounds->max_defined) bounds->max = 0;
        bounds->alignment = a_bounds.alignment / b_bounds.alignment;
        bounds->trim_bounds_using_alignment();
    }

    bool denominator_non_zero =
        (no_overflow_int(op->type) &&
         ((b_bounds.min_defined && b_bounds.min > 0) ||
          (b_bounds.max_defined && b_bounds.max < 0) ||
          (b_bounds.alignment.remainder != 0)));

    if (may_simplify(op->type)) {

        int lanes = op->type.lanes();

        auto rewrite = IRMatcher::rewriter(IRMatcher::div(a, b), op->type);

        if (rewrite(IRMatcher::Overflow() / x, a) ||
            rewrite(x / IRMatcher::Overflow(), b) ||
            rewrite(x / 1, x) ||
            rewrite(c0 / c1, fold(c0 / c1)) ||
            (!op->type.is_float() && rewrite(x / 0, 0)) ||
            (!op->type.is_float() && denominator_non_zero && rewrite(x / x, 1)) ||
            rewrite(0 / x, 0) ||
            false) {
            return rewrite.result;
        }

        if (EVAL_IN_LAMBDA
            (rewrite(broadcast(x) / broadcast(y), broadcast(x / y, lanes)) ||
             rewrite(select(x, c0, c1) / c2, select(x, fold(c0/c2), fold(c1/c2))) ||
             (!op->type.is_float() &&
              rewrite(x / x, select(x == 0, 0, 1))) ||
             (no_overflow(op->type) &&
              (// Fold repeated division
               rewrite((x / c0) / c2, x / fold(c0 * c2),                          c0 > 0 && c2 > 0 && !overflows(c0 * c2)) ||
               rewrite((x / c0 + c1) / c2, (x + fold(c1 * c0)) / fold(c0 * c2),   c0 > 0 && c2 > 0 && !overflows(c0 * c2) && !overflows(c0 * c1)) ||
               rewrite((x * c0) / c1, x / fold(c1 / c0),                          c1 % c0 == 0 && c0 > 0 && c1 / c0 != 0) ||
               // Pull out terms that are a multiple of the denominator
               rewrite((x * c0) / c1, x * fold(c0 / c1),                          c0 % c1 == 0 && c1 > 0) ||

               rewrite((x * c0 + y) / c1, y / c1 + x * fold(c0 / c1),             c0 % c1 == 0 && c1 > 0) ||
               rewrite((x * c0 - y) / c1, (-y) / c1 + x * fold(c0 / c1),          c0 % c1 == 0 && c1 > 0) ||
               rewrite((y + x * c0) / c1, y / c1 + x * fold(c0 / c1),             c0 % c1 == 0 && c1 > 0) ||
               rewrite((y - x * c0) / c1, y / c1 - x * fold(c0 / c1),             c0 % c1 == 0 && c1 > 0) ||

               rewrite(((x * c0 + y) + z) / c1, (y + z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite(((x * c0 - y) + z) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite(((x * c0 + y) - z) / c1, (y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite(((x * c0 - y) - z) / c1, (-y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||

               rewrite(((y + x * c0) + z) / c1, (y + z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite(((y + x * c0) - z) / c1, (y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite(((y - x * c0) - z) / c1, (y - z) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite(((y - x * c0) + z) / c1, (y + z) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||

               rewrite((z + (x * c0 + y)) / c1, (z + y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((z + (x * c0 - y)) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((z - (x * c0 - y)) / c1, (z + y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((z - (x * c0 + y)) / c1, (z - y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||

               rewrite((z + (y + x * c0)) / c1, (z + y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((z - (y + x * c0)) / c1, (z - y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((z + (y - x * c0)) / c1, (z + y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((z - (y - x * c0)) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||

               // For the next depth, stick to addition
               rewrite((((x * c0 + y) + z) + w) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((((y + x * c0) + z) + w) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite(((z + (x * c0 + y)) + w) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite(((z + (y + x * c0)) + w) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((w + ((x * c0 + y) + z)) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((w + ((y + x * c0) + z)) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((w + (z + (x * c0 + y))) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((w + (z + (y + x * c0))) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||

               // Finally, pull out constant additions that are a multiple of the denominator
               rewrite((x + c0) / c1, x / c1 + fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
               rewrite((c0 - y)/c1, fold(c0 / c1) - y / c1, (c0 + 1) % c1 == 0 && c1 > 0) ||
               (denominator_non_zero &&
                (rewrite((x + y)/x, y/x + 1) ||
                 rewrite((y + x)/x, y/x + 1) ||
                 rewrite((x - y)/x, (-y)/x + 1) ||
                 rewrite((y - x)/x, y/x - 1) ||
                 rewrite(((x + y) + z)/x, (y + z)/x + 1) ||
                 rewrite(((y + x) + z)/x, (y + z)/x + 1) ||
                 rewrite((z + (x + y))/x, (z + y)/x + 1) ||
                 rewrite((z + (y + x))/x, (z + y)/x + 1) ||
                 rewrite((x*y)/x, y) ||
                 rewrite((y*x)/x, y) ||
                 rewrite((x*y + z)/x, y + z/x) ||
                 rewrite((y*x + z)/x, y + z/x) ||
                 rewrite((z + x*y)/x, z/x + y) ||
                 rewrite((z + y*x)/x, z/x + y) ||
                 rewrite((x*y - z)/x, y + (-z)/x) ||
                 rewrite((y*x - z)/x, y + (-z)/x) ||
                 rewrite((z - x*y)/x, z/x - y) ||
                 rewrite((z - y*x)/x, z/x - y) ||
                 false)) ||

               (op->type.is_float() && rewrite(x/c0, x * fold(1/c0))))) ||
             (no_overflow_int(op->type) &&
              (rewrite(ramp(x, c0) / broadcast(c1), ramp(x / c1, fold(c0 / c1), lanes), c0 % c1 == 0) ||
               rewrite(ramp(x, c0) / broadcast(c1), broadcast(x / c1, lanes),
                       // First and last lanes are the same when...
                       can_prove((x % c1 + c0 * (lanes - 1)) / c1 == 0, this)))) ||
             (no_overflow_scalar_int(op->type) &&
              (rewrite(x / -1, -x) ||
               rewrite(c0 / y, select(y < 0, fold(-c0), c0), c0 == -1 ) ||
               rewrite((x * c0 + c1) / c2,
                       (x + fold(c1 / c0)) / fold(c2 / c0),
                       c2 > 0 && c0 > 0 && c2 % c0 == 0) ||
               rewrite((x * c0 + c1) / c2,
                       x * fold(c0 / c2) + fold(c1 / c2),
                       c2 > 0 && c0 % c2 == 0) ||
               // A very specific pattern that comes up in bounds in upsampling code.
               rewrite((x % 2 + c0) / 2, x % 2 + fold(c0 / 2), c0 % 2 == 1))))) {
            return mutate(std::move(rewrite.result), bounds);
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Div::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
