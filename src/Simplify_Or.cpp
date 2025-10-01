#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Or *op, ExprInfo *info) {
    if (truths.count(op)) {
        return const_true(op->type.lanes(), info);
    }

    Expr a = mutate(op->a, nullptr);
    Expr b = mutate(op->b, nullptr);

    if (should_commute(a, b)) {
        std::swap(a, b);
    }

    if (info) {
        info->cast_to(op->type);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::or_op(a, b), op->type);

    // clang-format off

    // Cases that fold to a constant
    if (EVAL_IN_LAMBDA
        (rewrite(x || true, true) ||

         rewrite(x || neg(x), true) ||
         rewrite(x || !(x && y), true) ||
         rewrite(x || (neg(x) || y), true) ||
         rewrite(x || (y || neg(x)), true) ||
         rewrite(!(x && y) || x, true) ||
         rewrite((x || y) || neg(x), true) ||
         rewrite((y || x) || neg(x), true) ||
         rewrite(x || !((x && z) && y), true) ||
         rewrite(x || (!(x && z) || y), true) ||
         rewrite(x || (y || !(x && z)), true) ||
         rewrite(x || (!(z && x) || y), true) ||
         rewrite(x || (y || !(z && x)), true) ||
         rewrite(x || ((neg(x) || z) || y), true) ||
         rewrite(x || (y || (neg(x) || z)), true) ||
         rewrite(x || ((z || neg(x)) || y), true) ||
         rewrite(x || (y || (z || neg(x))), true) ||
         rewrite(!(x && y) || (x || z), true) ||
         rewrite(!(x && y) || (z || x), true) ||
         rewrite((x || y) || (neg(x) || z), true) ||
         rewrite((x || y) || (z || neg(x)), true) ||
         rewrite((y || x) || (neg(x) || z), true) ||
         rewrite((y || x) || (z || neg(x)), true) ||
         rewrite(!((x && z) && y) || x, true) ||
         rewrite((!(x && z) || y) || x, true) ||
         rewrite((y || !(x && z)) || x, true) ||
         rewrite((!(z && x) || y) || x, true) ||
         rewrite((y || !(z && x)) || x, true) ||
         rewrite(((x || z) || y) || neg(x), true) ||
         rewrite((y || (x || z)) || neg(x), true) ||
         rewrite(((z || x) || y) || neg(x), true) ||
         rewrite((y || (z || x)) || neg(x), true) ||

         rewrite(x <= c0 || c1 <= x, true, !is_float(x) && c1 <= c0 + 1) ||
         rewrite(c1 <= x || x <= c0, true, !is_float(x) && c1 <= c0 + 1) ||
         rewrite(x <= c0 || c1 < x, true, c1 <= c0) ||
         rewrite(c1 <= x || x < c0, true, c1 <= c0) ||
         rewrite(x < c0 || c1 < x, true, c1 < c0) ||
         rewrite(c1 < x || x < c0, true, c1 < c0))) {
        set_expr_info_to_constant(info, true);
        return rewrite.result;
    }

    // Cases that fold to one of the args
    if (EVAL_IN_LAMBDA
        (rewrite(x || false, a) ||

         rewrite(x || x, a) ||
         rewrite(x || (x && y), a) ||
         rewrite(x || (y && x), a) ||
         rewrite(x || (x || y), b) ||
         rewrite(x || (y || x), b) ||
         rewrite((x && y) || x, b) ||
         rewrite((y && x) || x, b) ||
         rewrite((x || y) || x, a) ||
         rewrite((y || x) || x, a) ||
         rewrite(x || ((x && z) && y), a) ||
         rewrite(x || (y && (x && z)), a) ||
         rewrite(x || (!(neg(x) && z) || y), b) ||
         rewrite(x || (y || !(neg(x) && z)), b) ||
         rewrite(x || ((z && x) && y), a) ||
         rewrite(x || (y && (z && x)), a) ||
         rewrite(x || (!(z && neg(x)) || y), b) ||
         rewrite(x || (y || !(z && neg(x))), b) ||
         rewrite(x || ((x || z) || y), b) ||
         rewrite(x || (y || (x || z)), b) ||
         rewrite(x || (!(neg(x) || z) && y), a) ||
         rewrite(x || (y && !(neg(x) || z)), a) ||
         rewrite(x || ((z || x) || y), b) ||
         rewrite(x || (y || (z || x)), b) ||
         rewrite(x || (!(z || neg(x)) && y), a) ||
         rewrite(x || (y && !(z || neg(x))), a) ||
         rewrite((x || y) || (x && z), a) ||
         rewrite((x || y) || (z && x), a) ||
         rewrite((y || x) || (x && z), a) ||
         rewrite((y || x) || (z && x), a) ||
         rewrite(((x && z) && y) || x, b) ||
         rewrite((y && (x && z)) || x, b) ||
         rewrite((!(x && z) || y) || neg(x), a) ||
         rewrite((y || !(x && z)) || neg(x), a) ||
         rewrite(((z && x) && y) || x, b) ||
         rewrite((y && (z && x)) || x, b) ||
         rewrite((!(z && x) || y) || neg(x), a) ||
         rewrite((y || !(z && x)) || neg(x), a) ||
         rewrite(((x || z) || y) || x, a) ||
         rewrite((y || (x || z)) || x, a) ||
         rewrite((!(x || z) && y) || neg(x), b) ||
         rewrite((y && !(x || z)) || neg(x), b) ||
         rewrite(((z || x) || y) || x, a) ||
         rewrite((y || (z || x)) || x, a) ||
         rewrite((!(z || x) && y) || neg(x), b) ||
         rewrite((y && !(z || x)) || neg(x), b) ||

         rewrite(x != c0 || x == c1, a, c0 != c1) ||
         rewrite(c0 < x || c1 < x, fold(min(c0, c1)) < x) ||
         rewrite(c0 <= x || c1 <= x, fold(min(c0, c1)) <= x) ||
         rewrite(x < c0 || x < c1, x < fold(max(c0, c1))) ||
         rewrite(x <= c0 || x <= c1, x <= fold(max(c0, c1))))) {
        return rewrite.result;
    }


    // Cases that need remutation
    if (EVAL_IN_LAMBDA
        (rewrite(broadcast(x, c0) || broadcast(y, c0), broadcast(x || y, c0)) ||
         rewrite((x || broadcast(y, c0)) || broadcast(z, c0), x || broadcast(y || z, c0)) ||
         rewrite((broadcast(x, c0) || y) || broadcast(z, c0), broadcast(x || z, c0) || y) ||

         rewrite(!x && !y, !(x || y)) ||

         rewrite(x || (neg(x) && y), x || y) ||
         rewrite(x || (y && neg(x)), x || y) ||
         rewrite(x || !(x || y), !y || x) ||
         rewrite((x && y) || neg(x), !x || y) ||
         rewrite((y && x) || neg(x), !x || y) ||
         rewrite(!(x || y) || x, !y || x) ||
         rewrite(x || ((neg(x) && z) && y), (y && z) || x) ||
         rewrite(x || (y && (neg(x) && z)), (y && z) || x) ||
         rewrite(x || ((x && z) || y), x || y) ||
         rewrite(x || ((neg(x) && z) || y), (y || z) || x) ||
         rewrite(x || !((x && z) || y), !y || x) ||
         rewrite(x || (y || (x && z)), x || y) ||
         rewrite(x || (y || (neg(x) && z)), (y || z) || x) ||
         rewrite(x || (!(x && z) && y), x || y) ||
         rewrite(x || (!(neg(x) && z) && y), (!z && y) || x) ||
         rewrite(x || (y && !(x && z)), x || y) ||
         rewrite(x || (y && !(neg(x) && z)), (!z && y) || x) ||
         rewrite(x || ((z && neg(x)) && y), (y && z) || x) ||
         rewrite(x || (y && (z && neg(x))), (y && z) || x) ||
         rewrite(x || ((z && x) || y), x || y) ||
         rewrite(x || ((z && neg(x)) || y), (y || z) || x) ||
         rewrite(x || (y || (z && x)), x || y) ||
         rewrite(x || (y || (z && neg(x))), (y || z) || x) ||
         rewrite(x || (!(z && x) && y), x || y) ||
         rewrite(x || (!(z && neg(x)) && y), (!z && y) || x) ||
         rewrite(x || (y && !(z && x)), x || y) ||
         rewrite(x || (y && !(z && neg(x))), (!z && y) || x) ||
         rewrite(x || ((x || z) && y), (y && z) || x) ||
         rewrite(x || ((neg(x) || z) && y), x || y) ||
         rewrite(x || !((x || z) && y), (!y || !z) || x) ||
         rewrite(x || (y && (x || z)), (y && z) || x) ||
         rewrite(x || (y && (neg(x) || z)), x || y) ||
         rewrite(x || !((x || z) || y), !(y || z) || x) ||
         rewrite(x || (!(x || z) && y), (!z && y) || x) ||
         rewrite(x || (!(x || z) || y), (!z || y) || x) ||
         rewrite(x || (!(neg(x) || z) || y), x || y) ||
         rewrite(x || (y && !(x || z)), (!z && y) || x) ||
         rewrite(x || (y || !(x || z)), (!z || y) || x) ||
         rewrite(x || (y || !(neg(x) || z)), x || y) ||
         rewrite(x || ((z || x) && y), (y && z) || x) ||
         rewrite(x || ((z || neg(x)) && y), x || y) ||
         rewrite(x || (y && (z || x)), (y && z) || x) ||
         rewrite(x || (y && (z || neg(x))), x || y) ||
         rewrite(x || (!(z || x) && y), (!z && y) || x) ||
         rewrite(x || (!(z || x) || y), (!z || y) || x) ||
         rewrite(x || (!(z || neg(x)) || y), x || y) ||
         rewrite(x || (y && !(z || x)), (!z && y) || x) ||
         rewrite(x || (y || !(z || x)), (!z || y) || x) ||
         rewrite(x || (y || !(z || neg(x))), x || y) ||
         rewrite((x && y) || (x && z), (y || z) && x) ||
         rewrite((x && y) || (neg(x) && z), select(x, y, z)) ||
         rewrite(!(x && y) || (x && z), !x || (!y || z)) ||
         rewrite((x && y) || (z && x), (y || z) && x) ||
         rewrite((x && y) || (z && neg(x)), select(x, y, z)) ||
         rewrite(!(x && y) || (z && x), !x || (!y || z)) ||
         rewrite((y && x) || (x && z), (y || z) && x) ||
         rewrite((y && x) || (neg(x) && z), select(x, y, z)) ||
         rewrite((y && x) || (z && x), (y || z) && x) ||
         rewrite((y && x) || (z && neg(x)), select(x, y, z)) ||
         rewrite((x || y) || (neg(x) && z), (y || z) || x) ||
         rewrite(!(x || y) || (x && z), select(x, z, !y)) ||
         rewrite((x || y) || (z && neg(x)), (y || z) || x) ||
         rewrite(!(x || y) || (z && x), select(x, z, !y)) ||
         rewrite((x || y) || (x || z), (y || z) || x) ||
         rewrite(!(x || y) || (x || z), (!y || z) || x) ||
         rewrite((x || y) || (z || x), (y || z) || x) ||
         rewrite(!(x || y) || (z || x), (!y || z) || x) ||
         rewrite((y || x) || (neg(x) && z), (y || z) || x) ||
         rewrite((y || x) || (z && neg(x)), (y || z) || x) ||
         rewrite((y || x) || (x || z), (y || z) || x) ||
         rewrite((y || x) || (z || x), (y || z) || x) ||
         rewrite(((x && z) && y) || neg(x), !x || (y && z)) ||
         rewrite((y && (x && z)) || neg(x), !x || (y && z)) ||
         rewrite(((x && z) || y) || x, x || y) ||
         rewrite(((x && z) || y) || neg(x), !x || (y || z)) ||
         rewrite(!((x && z) || y) || x, !y || x) ||
         rewrite((y || (x && z)) || x, x || y) ||
         rewrite((y || (x && z)) || neg(x), !x || (y || z)) ||
         rewrite((!(x && z) && y) || x, x || y) ||
         rewrite((!(x && z) && y) || neg(x), !x || (!z && y)) ||
         rewrite((y && !(x && z)) || x, x || y) ||
         rewrite((y && !(x && z)) || neg(x), !x || (!z && y)) ||
         rewrite(((z && x) && y) || neg(x), !x || (y && z)) ||
         rewrite((y && (z && x)) || neg(x), !x || (y && z)) ||
         rewrite(((z && x) || y) || x, x || y) ||
         rewrite(((z && x) || y) || neg(x), !x || (y || z)) ||
         rewrite((y || (z && x)) || x, x || y) ||
         rewrite((y || (z && x)) || neg(x), !x || (y || z)) ||
         rewrite((!(z && x) && y) || x, x || y) ||
         rewrite((!(z && x) && y) || neg(x), !x || (!z && y)) ||
         rewrite((y && !(z && x)) || x, x || y) ||
         rewrite((y && !(z && x)) || neg(x), !x || (!z && y)) ||
         rewrite(((x || z) && y) || x, (y && z) || x) ||
         rewrite(((x || z) && y) || neg(x), !x || y) ||
         rewrite(!((x || z) && y) || x, (!y || !z) || x) ||
         rewrite((y && (x || z)) || x, (y && z) || x) ||
         rewrite((y && (x || z)) || neg(x), !x || y) ||
         rewrite(!((x || z) || y) || x, !(y || z) || x) ||
         rewrite((!(x || z) && y) || x, (!z && y) || x) ||
         rewrite((!(x || z) || y) || x, (!z || y) || x) ||
         rewrite((!(x || z) || y) || neg(x), !x || y) ||
         rewrite((y && !(x || z)) || x, (!z && y) || x) ||
         rewrite((y || !(x || z)) || x, (!z || y) || x) ||
         rewrite((y || !(x || z)) || neg(x), !x || y) ||
         rewrite(((z || x) && y) || x, (y && z) || x) ||
         rewrite(((z || x) && y) || neg(x), !x || y) ||
         rewrite((y && (z || x)) || x, (y && z) || x) ||
         rewrite((y && (z || x)) || neg(x), !x || y) ||
         rewrite((!(z || x) && y) || x, (!z && y) || x) ||
         rewrite((!(z || x) || y) || x, (!z || y) || x) ||
         rewrite((!(z || x) || y) || neg(x), !x || y) ||
         rewrite((y && !(z || x)) || x, (!z && y) || x) ||
         rewrite((y || !(z || x)) || x, (!z || y) || x) ||
         rewrite((y || !(z || x)) || neg(x), !x || y) ||

         rewrite(x < y || x < z, x < max(y, z)) ||
         rewrite(y < x || z < x, min(y, z) < x) ||
         rewrite(x <= y || x <= z, x <= max(y, z)) ||
         rewrite(y <= x || z <= x, min(y, z) <= x) ||

         false)) {

        return mutate(rewrite.result, info);
    }

    // clang-format on

    if (a.same_as(op->a) &&
        b.same_as(op->b)) {
        return op;
    } else {
        return Or::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
