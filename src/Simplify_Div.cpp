#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Div *op, ConstBounds *bounds) {
    ConstBounds a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds && no_overflow_int(op->type)) {
        bounds->min_defined = bounds->max_defined =
            (a_bounds.min_defined && b_bounds.min_defined &&
             a_bounds.max_defined && b_bounds.max_defined &&
             (b_bounds.min > 0 || b_bounds.max < 0));
        if (bounds->min_defined) {
            int64_t v1 = div_imp(a_bounds.min, b_bounds.min);
            int64_t v2 = div_imp(a_bounds.min, b_bounds.max);
            int64_t v3 = div_imp(a_bounds.max, b_bounds.min);
            int64_t v4 = div_imp(a_bounds.max, b_bounds.max);
            bounds->min = std::min(std::min(v1, v2), std::min(v3, v4));
            bounds->max = std::max(std::max(v1, v2), std::max(v3, v4));

            // Bounded numerator divided by constantish
            // denominator can sometimes collapse things to a
            // constant at this point.
            if (bounds->min == bounds->max) {
                return make_const(op->type, bounds->min);
            }
        }
    }

    if (may_simplify(op->type)) {

        int lanes = op->type.lanes();

        auto rewrite = IRMatcher::rewriter(IRMatcher::div(a, b), op->type);

        if (rewrite(IRMatcher::Indeterminate() / x, a) ||
            rewrite(x / IRMatcher::Indeterminate(), b) ||
            rewrite(IRMatcher::Overflow() / x, a) ||
            rewrite(x / IRMatcher::Overflow(), b) ||
            rewrite(x / 1, x) ||
            (!op->type.is_float() &&
             rewrite(x / 0, IRMatcher::Indeterminate())) ||
            rewrite(0 / x, 0) ||
            rewrite(x / x, 1) ||
            rewrite(c0 / c1, fold(c0 / c1))) {
            return rewrite.result;
        }

        if (EVAL_IN_LAMBDA
            (rewrite(broadcast(x) / broadcast(y), broadcast(x / y, lanes)) ||
             rewrite(select(x, c0, c1) / c2, select(x, fold(c0/c2), fold(c1/c2))) ||
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

               rewrite((x + c0) / c1, x / c1 + fold(c0 / c1),                     c0 % c1 == 0) ||
               rewrite((x + y)/x, y/x + 1) ||
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

}
}
