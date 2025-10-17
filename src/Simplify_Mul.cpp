#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Mul *op, ExprInfo *info) {
    ExprInfo a_info, b_info, mul_info;
    Expr a = mutate(op->a, &a_info);
    Expr b = mutate(op->b, &b_info);

    if (op->type.is_int_or_uint()) {
        mul_info.bounds = a_info.bounds * b_info.bounds;
        mul_info.alignment = a_info.alignment * b_info.alignment;
        mul_info.cast_to(op->type);
        mul_info.trim_bounds_using_alignment();
    }

    if (info) {
        *info = mul_info;
    }

    if (!no_overflow(op->type) &&
        mul_info.bounds.is_single_point()) {
        // For types with defined overflow, it's possible for a multiply to turn
        // something into a constant without either arg being a zero
        // (e.g. select(x, 64, 128)*4 is zero in uint8).
        return make_const(op->type, mul_info.bounds.min, nullptr);
    }

    // Order commutative operations by node type
    if (should_commute(a, b)) {
        std::swap(a, b);
        std::swap(a_info, b_info);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::mul(a, b), op->type);

    if (rewrite(IRMatcher::Overflow() * x, a) ||
        rewrite(x * IRMatcher::Overflow(), b)) {
        clear_expr_info(info);
        return rewrite.result;
    }

    if (rewrite(0 * x, 0) ||
        rewrite(1 * x, x) ||
        rewrite(x * 0, 0) ||
        rewrite(x * 1, x)) {
        return rewrite.result;
    }

    if (rewrite(c0 * c1, fold(c0 * c1)) ||
        (!no_overflow(op->type) &&  // Intentionally-overflowing quadratics used in random number generation
         (rewrite((x + c0) * (x + c1), x * (x + fold(c0 + c1)) + fold(c0 * c1)) ||
          rewrite((x * c0 + c1) * (x + c2), x * (x * c0 + fold(c1 + c0 * c2)) + fold(c1 * c2)) ||
          rewrite((x + c2) * (x * c0 + c1), x * (x * c0 + fold(c1 + c0 * c2)) + fold(c1 * c2)) ||
          rewrite((x * c0 + c1) * (x * c2 + c3), x * (x * fold(c0 * c2) + fold(c0 * c3 + c1 * c2)) + fold(c1 * c3)))) ||
        rewrite((x + c0) * c1, x * c1 + fold(c0 * c1), !overflows(c0 * c1)) ||
        rewrite((c0 - x) * c1, x * fold(-c1) + fold(c0 * c1), !overflows(c0 * c1)) ||
        rewrite((0 - x) * y, 0 - x * y) ||
        rewrite(x * (0 - y), 0 - x * y) ||
        rewrite((x - y) * c0, (y - x) * fold(-c0), c0 < 0 && -c0 > 0) ||
        rewrite((x * c0) * c1, x * fold(c0 * c1), !overflows(c0 * c1)) ||
        rewrite((x * c0) * y, (x * y) * c0, !is_const(y)) ||
        rewrite(x * (y * c0), (x * y) * c0) ||
        rewrite(max(x, y) * min(x, y), x * y) ||
        rewrite(max(x, y) * min(y, x), y * x) ||

        rewrite(x * select(y, 1, 0), select(y, x, 0)) ||
        rewrite(select(x, 1, 0) * y, select(x, y, 0)) ||

        rewrite(broadcast(x, c0) * broadcast(y, c0), broadcast(x * y, c0)) ||
        rewrite(broadcast(x, c0) * broadcast(y, c1), broadcast(x * broadcast(y, fold(c1 / c0)), c0), c1 % c0 == 0) ||
        rewrite(broadcast(y, c1) * broadcast(x, c0), broadcast(broadcast(y, fold(c1 / c0)) * x, c0), c1 % c0 == 0) ||
        rewrite(ramp(x, y, c0) * broadcast(z, c0), ramp(x * z, y * z, c0)) ||
        rewrite(ramp(broadcast(x, c0), broadcast(y, c0), c1) * broadcast(z, c2),
                ramp(broadcast(x * z, c0), broadcast(y * z, c0), c1), c2 == c0 * c1) ||

        // Hoist shuffles. The Shuffle visitor wants to sink
        // extract_elements to the leaves, and those count as degenerate
        // slices, so only hoist shuffles that grab more than one lane.
        rewrite(slice(x, c0, c1, c2) * slice(y, c0, c1, c2), slice(x * y, c0, c1, c2), c2 > 1 && lanes_of(x) == lanes_of(y)) ||
        rewrite(slice(x, c0, c1, c2) * (slice(y, c0, c1, c2) * z), slice(x * y, c0, c1, c2) * z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||
        rewrite(slice(x, c0, c1, c2) * (z * slice(y, c0, c1, c2)), slice(x * y, c0, c1, c2) * z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||

        false) {
        return mutate(rewrite.result, info);
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Mul::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
