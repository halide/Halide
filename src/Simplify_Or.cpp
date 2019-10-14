#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Or *op, ExprInfo *bounds) {
    if (truths.count(op)) {
        return const_true(op->type.lanes());
    }

    Expr a = mutate(op->a, nullptr);
    Expr b = mutate(op->b, nullptr);

    if (should_commute(a, b)) {
        std::swap(a, b);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::or_op(a, b), op->type);

    if (EVAL_IN_LAMBDA
        (rewrite(x || true, b) ||
         rewrite(x || false, a) ||
         rewrite(x || x, a) ||

         rewrite((x || y) || x, a) ||
         rewrite(x || (x || y), b) ||
         rewrite((x || y) || y, a) ||
         rewrite(y || (x || y), b) ||

         rewrite(((x || y) || z) || x, a) ||
         rewrite(x || ((x || y) || z), b) ||
         rewrite((z || (x || y)) || x, a) ||
         rewrite(x || (z || (x || y)), b) ||
         rewrite(((x || y) || z) || y, a) ||
         rewrite(y || ((x || y) || z), b) ||
         rewrite((z || (x || y)) || y, a) ||
         rewrite(y || (z || (x || y)), b) ||

         rewrite((x && y) || x, b) ||
         rewrite(x || (x && y), a) ||
         rewrite((x && y) || y, b) ||
         rewrite(y || (x && y), a) ||

         rewrite(((x || y) || z) || x, a) ||
         rewrite(x || ((x || y) || z), b) ||
         rewrite((z || (x || y)) || x, a) ||
         rewrite(x || (z || (x || y)), b) ||
         rewrite(((x || y) || z) || y, a) ||
         rewrite(y || ((x || y) || z), b) ||
         rewrite((z || (x || y)) || y, a) ||
         rewrite(y || (z || (x || y)), b) ||

         rewrite(x != y || x == y, true) ||
         rewrite(x != y || y == x, true) ||
         rewrite((z || x != y) || x == y, true) ||
         rewrite((z || x != y) || y == x, true) ||
         rewrite((x != y || z) || x == y, true) ||
         rewrite((x != y || z) || y == x, true) ||
         rewrite((z || x == y) || x != y, true) ||
         rewrite((z || x == y) || y != x, true) ||
         rewrite((x == y || z) || x != y, true) ||
         rewrite((x == y || z) || y != x, true) ||
         rewrite(x || !x, true) ||
         rewrite(!x || x, true) ||
         rewrite(y <= x || x < y, true) ||
         rewrite(x != c0 || x == c1, a, c0 != c1) ||
         rewrite(x <= c0 || c1 <= x, true, !is_float(x) && c1 <= c0 + 1) ||
         rewrite(c1 <= x || x <= c0, true, !is_float(x) && c1 <= c0 + 1) ||
         rewrite(x <= c0 || c1 < x, true, c1 <= c0) ||
         rewrite(c1 <= x || x < c0, true, c1 <= c0) ||
         rewrite(x < c0 || c1 < x, true, c1 < c0) ||
         rewrite(c1 < x || x < c0, true, c1 < c0) ||
         rewrite(c0 < x || c1 < x, fold(min(c0, c1)) < x) ||
         rewrite(c0 <= x || c1 <= x, fold(min(c0, c1)) <= x) ||
         rewrite(x < c0 || x < c1, x < fold(max(c0, c1))) ||
         rewrite(x <= c0 || x <= c1, x <= fold(max(c0, c1))))) {
        return rewrite.result;
    }

    if (rewrite(broadcast(x) || broadcast(y), broadcast(x || y, op->type.lanes())) ||

        rewrite(x < y || y < x, x != y) ||

        rewrite((x && (y || z)) || y, (x && z) || y) ||
        rewrite((x && (z || y)) || y, (x && z) || y) ||
        rewrite(y || (x && (y || z)), y || (x && z)) ||
        rewrite(y || (x && (z || y)), y || (x && z)) ||

        rewrite(((y || z) && x) || y, (z && x) || y) ||
        rewrite(((z || y) && x) || y, (z && x) || y) ||
        rewrite(y || ((y || z) && x), y || (z && x)) ||
        rewrite(y || ((z || y) && x), y || (z && x)) ||

        rewrite((x || (y && z)) || y, x || y) ||
        rewrite((x || (z && y)) || y, x || y) ||
        rewrite(y || (x || (y && z)), y || x) ||
        rewrite(y || (x || (z && y)), y || x) ||

        rewrite(((y && z) || x) || y, x || y) ||
        rewrite(((z && y) || x) || y, x || y) ||
        rewrite(y || ((y && z) || x), y || x) ||
        rewrite(y || ((z && y) || x), y || x) ||

        rewrite((x && y) || (x && z), x && (y || z)) ||
        rewrite((x && y) || (z && x), x && (y || z)) ||
        rewrite((y && x) || (x && z), x && (y || z)) ||
        rewrite((y && x) || (z && x), x && (y || z)) ||

        rewrite(x < y || x < z, x < max(y, z)) ||
        rewrite(y < x || z < x, min(y, z) < x) ||
        rewrite(x <= y || x <= z, x <= max(y, z)) ||
        rewrite(y <= x || z <= x, min(y, z) <= x)) {

        return mutate(std::move(rewrite.result), bounds);
    }

    #if USE_SYNTHESIZED_RULES
    if (rewrite((((x + y) <= z) || ((y + z) <= x)), (y <= max(x - z, z - x)), is_no_overflow_int(x)) ||
        rewrite(((x <= y) || ((y + c0) <= x)), true, (c0 <= 0) && is_no_overflow_int(x)) ||
        rewrite(((x <= (y + z)) || (((y + z) + c0) <= x)), true, (c0 <= 0) && is_no_overflow_int(x)) ||
        rewrite((((x + c0) <= y) || (y < x)), true, (c0 <= 0) && is_no_overflow_int(x)) ||
        rewrite(((x <= y) || ((y + c0) <= x)), true, (c0 <= 0) && is_no_overflow_int(x)) ||

        rewrite((((x + c0) <= y) || ((y + c1) <= x)), true, ((c0 + c1) <= 0) && is_no_overflow_int(x)) ||

        false) {
        return mutate(std::move(rewrite.result), bounds);
    }
    #endif

    if (a.same_as(op->a) &&
        b.same_as(op->b)) {
        return op;
    } else {
        return Or::make(a, b);
    }
}

}
}
