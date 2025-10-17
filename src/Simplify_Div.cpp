#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Div *op, ExprInfo *info) {
    ExprInfo a_info, b_info;
    Expr a = mutate(op->a, &a_info);
    Expr b = mutate(op->b, &b_info);

    if (info) {
        if (op->type.is_int_or_uint()) {
            // ConstantInterval division is integer division, so we can't use
            // this code path for floats.
            info->bounds = a_info.bounds / b_info.bounds;
            info->alignment = a_info.alignment / b_info.alignment;
            info->cast_to(op->type);
            info->trim_bounds_using_alignment();

            // Bounded numerator divided by constantish bounded denominator can
            // sometimes collapse things to a constant at this point. This
            // mostly happens when the denominator is a constant and the
            // numerator span is small (e.g. [23, 29]/10 = 2), but there are
            // also cases with a bounded denominator (e.g. [5, 7]/[4, 5] = 1).
            if (info->bounds.is_single_point()) {
                if (op->type.can_represent(info->bounds.min)) {
                    return make_const(op->type, info->bounds.min, nullptr);
                } else {
                    // Even though this is 'no-overflow-int', if the result
                    // we calculate can't fit into the destination type,
                    // we're better off returning an overflow condition than
                    // a known-wrong value. (Note that no_overflow_int() should
                    // only be true for signed integers.)
                    internal_assert(no_overflow_int(op->type));
                    clear_expr_info(info);
                    return make_signed_integer_overflow(op->type);
                }
            }
        } else {
            // TODO: Tracking constant integer bounds of floating point values
            // isn't so useful right now, but if we want integer bounds for
            // floating point division later, here's the place to put it.
            clear_expr_info(info);
        }
    }

    bool denominator_non_zero =
        (no_overflow_int(op->type) &&
         (!b_info.bounds.contains(0) ||
          b_info.alignment.remainder != 0));

    int lanes = op->type.lanes();

    auto rewrite = IRMatcher::rewriter(IRMatcher::div(a, b), op->type);

    if (rewrite(IRMatcher::Overflow() / x, a) ||
        rewrite(x / IRMatcher::Overflow(), b) ||
        rewrite(x / 1, x) ||
        rewrite(0 / x, 0) ||
        false) {
        return rewrite.result;
    }

    int a_mod = a_info.alignment.modulus;
    int a_rem = a_info.alignment.remainder;

    // clang-format off
    if (EVAL_IN_LAMBDA
        (rewrite(c0 / c1, fold(c0 / c1)) ||
         (!op->type.is_float() && rewrite(x / 0, 0)) ||
         (!op->type.is_float() && denominator_non_zero && rewrite(x / x, 1)) ||
         rewrite(broadcast(x, c0) / broadcast(y, c0), broadcast(x / y, c0)) ||
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
           rewrite(min((x * c0), c1) / c2, min(x * fold(c0 / c2), fold(c1 / c2)), c0 % c2 == 0 && c2 > 0) ||
           rewrite(max((x * c0), c1) / c2, max(x * fold(c0 / c2), fold(c1 / c2)), c0 % c2 == 0 && c2 > 0) ||

           rewrite((x * c0 + y) / c1, y / c1 + x * fold(c0 / c1),             c0 % c1 == 0 && c1 > 0) ||
           rewrite((x * c0 - y) / c0, x + (0 - y) / c0) ||
           rewrite((x * c1 - y) / c0, (0 - y) / c0 - x,                       c0 + c1 == 0) ||
           rewrite((y + x * c0) / c1, y / c1 + x * fold(c0 / c1),             c0 % c1 == 0 && c1 > 0) ||
           rewrite((y - x * c0) / c1, y / c1 - x * fold(c0 / c1),             c0 % c1 == 0 && c1 > 0) ||

           rewrite(((x * c0 + y) + z) / c1, (y + z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite(((x * c0 - y) + z) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite(((x * c0 + y) - z) / c1, (y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite(((x * c0 - y) - z) / c0, x + (0 - y - z) / c0) ||
           rewrite(((x * c1 - y) - z) / c0, (0 - y - z) / c0 - x,             c0 + c1 == 0) ||

           rewrite(((y + x * c0) + z) / c1, (y + z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite(((y + x * c0) - z) / c1, (y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite(((y - x * c0) - z) / c1, (y - z) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite(((y - x * c0) + z) / c1, (y + z) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||

           rewrite((z + (x * c0 + y)) / c1, (z + y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite((z + (x * c0 - y)) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite((z - (x * c0 - y)) / c1, (z + y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite((z - (x * c0 + y)) / c1, (z - y) / c1 + x * fold(-c0 / c1), c0 % c1 == 0 && c1 > 0) ||

           rewrite((z + (y + x * c0)) / c1, (z + y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
           rewrite((z - (y + x * c0)) / c1, (z - y) / c1 + x * fold(-c0 / c1), c0 % c1 == 0 && c1 > 0) ||
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

           /** In (x + c0) / c1, when can we pull the constant
               addition out of the numerator? An obvious answer is
               the constant is a multiple of the denominator, but
               there are other cases too. The condition for the
               rewrite to be correct is:

               (x + c0) / c1 == x / c1 + c2

               Say we know (x + c0) = a_mod * y + a_rem

               (a_mod * y + a_rem) / c1 == (a_mod * y + a_rem - c0) / c1 + c2

               If a_mod % c1 == 0, we can subtract the term in y
               from both sides and get:

               a_rem / c1 == (a_rem - c0) / c1 + c2

               c2 == a_rem / c1 - (a_rem - c0) / c1

               This is a sufficient and necessary condition for the case when x_mod % c1 == 0.
           */
           (no_overflow_int(op->type) &&
            (rewrite((x + c0) / c1, x / c1 + fold(a_rem / c1 - (a_rem - c0) / c1), a_mod % c1 == 0) ||

             /**
                Now do the same thing for subtraction from a constant.

                (c0 - x) / c1 == c2 - x / c1

                where c0 - x == a_mod * y + a_rem

                So x = c0 - a_mod * y - a_rem

                (a_mod * y + a_rem) / c1 == c2 - (c0 - a_mod * y - a_rem) / c1

                If a_mod % c1 == 0, we can pull that term out and cancel it:

                a_rem / c1 == c2 - (c0 - a_rem) / c1

                c2 == a_rem / c1 + (c0 - a_rem) / c1

             */
             rewrite((c0 - x)/c1, fold(a_rem / c1 + (c0 - a_rem) / c1) - x / c1, a_mod % c1 == 0) ||

             // We can also pull it out when the constant is a
             // multiple of the denominator.
             rewrite((x + c0) / c1, x / c1 + fold(c0 / c1), c0 % c1 == 0) ||
             rewrite((c0 - x) / c1, fold(c0 / c1) - x / c1, (c0 + 1) % c1 == 0))) ||

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
          (
           rewrite(ramp(x, c0, lanes) / broadcast(c1, lanes), ramp(x / c1, fold(c0 / c1), lanes), (c0 % c1 == 0)) ||
           rewrite(ramp(x, c0, lanes) / broadcast(c1, lanes), broadcast(x / c1, lanes),
                   // First and last lanes are the same when...
                   can_prove((x % c1 + c0 * (lanes - 1)) / c1 == 0, this))
           )) ||
         (no_overflow_scalar_int(op->type) &&
          (rewrite(x / -1, -x) ||
           (denominator_non_zero && rewrite(c0 / y, select(y < 0, fold(-c0), c0), c0 == -1)) ||
           rewrite((x * c0 + c1) / c2,
                   (x + fold(c1 / c0)) / fold(c2 / c0),
                   c2 > 0 && c0 > 0 && c2 % c0 == 0) ||
           rewrite((x * c0 + c1) / c2,
                   x * fold(c0 / c2) + fold(c1 / c2),
                   c2 > 0 && c0 % c2 == 0) ||
           // A very specific pattern that comes up in bounds in upsampling code.
           rewrite((x % 2 + c0) / 2, x % 2 + fold(c0 / 2), c0 % 2 == 1))))) {
        return mutate(rewrite.result, info);
    }
    // clang-format on

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Div::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
