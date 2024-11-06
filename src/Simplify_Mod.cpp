#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Mod *op, ExprInfo *info) {
    ExprInfo a_info, b_info;
    Expr a = mutate(op->a, &a_info);
    Expr b = mutate(op->b, &b_info);

    // We always combine bounds here, even if not requested, because
    // we can use them to simplify down to a constant if the bounds
    // are tight enough.
    ExprInfo mod_info;
    if (op->type.is_int_or_uint()) {
        mod_info.bounds = a_info.bounds % b_info.bounds;
        mod_info.alignment = a_info.alignment % b_info.alignment;
        mod_info.trim_bounds_using_alignment();
        // Modulo can't overflow, so no mod_info.cast_to(op->type)
    }
    // TODO: Modulo bounds for floating-point modulo
    if (info) {
        *info = mod_info;
    }

    if (may_simplify(op->type)) {
        if (a_info.bounds >= 0 && a_info.bounds < b_info.bounds) {
            return a;
        }

        if (mod_info.bounds.is_single_point()) {
            return make_const(op->type, mod_info.bounds.min);
        }

        int lanes = op->type.lanes();
        auto rewrite = IRMatcher::rewriter(IRMatcher::mod(a, b), op->type);

        if (rewrite(c0 % c1, fold(c0 % c1)) ||
            rewrite(IRMatcher::Overflow() % x, a) ||
            rewrite(x % IRMatcher::Overflow(), b) ||
            rewrite(0 % x, 0) ||
            rewrite(x % x, 0) ||
            rewrite(x % 0, 0) ||
            (!op->type.is_float() &&
             rewrite(x % 1, 0))) {
            return rewrite.result;
        }

        // clang-format off
        if (EVAL_IN_LAMBDA
            (rewrite(broadcast(x, c0) % broadcast(y, c0), broadcast(x % y, c0)) ||
             (no_overflow_int(op->type) &&
              (rewrite((x * c0) % c1, (x * fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               rewrite((x + c0) % c1, (x + fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               rewrite((x * c0) % c1, (x % fold(c1/c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
               rewrite((x * c0 + y) % c1, y % c1, c0 % c1 == 0) ||
               rewrite((y + x * c0) % c1, y % c1, c0 % c1 == 0) ||
               rewrite((x * c0 - y) % c1, (-y) % c1, c0 % c1 == 0) ||
               rewrite((y - x * c0) % c1, y % c1, c0 % c1 == 0) ||
               rewrite((x - y) % 2, (x + y) % 2) || // Addition and subtraction are the same modulo 2, because -1 == 1

               rewrite(ramp(x, c0, c2) % broadcast(c1, c2), broadcast(x, c2) % broadcast(c1, c2), (c0 % c1 == 0)) ||
               rewrite(ramp(x, c0, lanes) % broadcast(c1, lanes), ramp(x % c1, c0, lanes),
                       // First and last lanes are the same when...
                       can_prove((x % c1 + c0 * (lanes - 1)) / c1 == 0, this)) ||
               rewrite(ramp(x * c0, c2, c3) % broadcast(c1, c3),
                       ramp(x * fold(c0 % c1), fold(c2 % c1), c3) % broadcast(c1, c3),
                       c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               rewrite(ramp(x + c0, c2, c3) % broadcast(c1, c3),
                       ramp(x + fold(c0 % c1), fold(c2 % c1), c3) % broadcast(c1, c3),
                       c1 > 0 && (c0 >= c1 || c0 < 0)) ||
               rewrite(ramp(x * c0 + y, c2, c3) % broadcast(c1, c3),
                       ramp(y, fold(c2 % c1), c3) % broadcast(c1, c3),
                       c0 % c1 == 0) ||
               rewrite(ramp(y + x * c0, c2, c3) % broadcast(c1, c3),
                       ramp(y, fold(c2 % c1), c3) % broadcast(c1, c3),
                       c0 % c1 == 0))))) {
            return mutate(rewrite.result, info);
        }
        // clang-format on
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Mod::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
