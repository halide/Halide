#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const And *op, ExprInfo *bounds) {
    if (falsehoods.count(op)) {
        return const_false(op->type.lanes());
    }

    Expr a = mutate(op->a, nullptr);
    Expr b = mutate(op->b, nullptr);

    // Order commutative operations by node type
    if (should_commute(a, b)) {
        std::swap(a, b);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::and_op(a, b), op->type);

    // clang-format off
    if (EVAL_IN_LAMBDA
        (rewrite(x && true, a) ||
         rewrite(x && false, b) ||
         rewrite(x && x, a) ||

         rewrite((x && y) && x, a) ||
         rewrite(x && (x && y), b) ||
         rewrite((x && y) && y, a) ||
         rewrite(y && (x && y), b) ||

         rewrite(((x && y) && z) && x, a) ||
         rewrite(x && ((x && y) && z), b) ||
         rewrite((z && (x && y)) && x, a) ||
         rewrite(x && (z && (x && y)), b) ||
         rewrite(((x && y) && z) && y, a) ||
         rewrite(y && ((x && y) && z), b) ||
         rewrite((z && (x && y)) && y, a) ||
         rewrite(y && (z && (x && y)), b) ||

         rewrite((x || y) && x, b) ||
         rewrite(x && (x || y), a) ||
         rewrite((x || y) && y, b) ||
         rewrite(y && (x || y), a) ||

         rewrite(x != y && x == y, false) ||
         rewrite(x != y && y == x, false) ||
         rewrite((z && x != y) && x == y, false) ||
         rewrite((z && x != y) && y == x, false) ||
         rewrite((x != y && z) && x == y, false) ||
         rewrite((x != y && z) && y == x, false) ||
         rewrite((z && x == y) && x != y, false) ||
         rewrite((z && x == y) && y != x, false) ||
         rewrite((x == y && z) && x != y, false) ||
         rewrite((x == y && z) && y != x, false) ||
         rewrite(x && !x, false) ||
         rewrite(!x && x, false) ||
         rewrite(y <= x && x < y, false) ||
         rewrite(y < x && x < y, false) ||
         rewrite(x != c0 && x == c1, b, c0 != c1) ||
         rewrite(x == c0 && x == c1, false, c0 != c1) ||
         // Note: In the predicate below, if undefined overflow
         // occurs, the predicate counts as false. If well-defined
         // overflow occurs, the condition couldn't possibly
         // trigger because c0 + 1 will be the smallest possible
         // value.
         rewrite(c0 < x && x < c1, false, !is_float(x) && c1 <= c0 + 1) ||
         rewrite(x < c1 && c0 < x, false, !is_float(x) && c1 <= c0 + 1) ||
         rewrite(x <= c1 && c0 < x, false, c1 <= c0) ||
         rewrite(c0 <= x && x < c1, false, c1 <= c0) ||
         rewrite(c0 <= x && x <= c1, false, c1 < c0) ||
         rewrite(x <= c1 && c0 <= x, false, c1 < c0) ||
         rewrite(c0 < x && c1 < x, fold(max(c0, c1)) < x) ||
         rewrite(c0 <= x && c1 <= x, fold(max(c0, c1)) <= x) ||
         rewrite(x < c0 && x < c1, x < fold(min(c0, c1))) ||
         rewrite(x <= c0 && x <= c1, x <= fold(min(c0, c1))))) {
        return rewrite.result;
    }
    // clang-format on

    if (rewrite(broadcast(x, c0) && broadcast(y, c0), broadcast(x && y, c0)) ||
        rewrite((x || (y && z)) && y, (x || z) && y) ||
        rewrite((x || (z && y)) && y, (x || z) && y) ||
        rewrite(y && (x || (y && z)), y && (x || z)) ||
        rewrite(y && (x || (z && y)), y && (x || z)) ||

        rewrite(((y && z) || x) && y, (z || x) && y) ||
        rewrite(((z && y) || x) && y, (z || x) && y) ||
        rewrite(y && ((y && z) || x), y && (z || x)) ||
        rewrite(y && ((z && y) || x), y && (z || x)) ||

        rewrite((x && (y || z)) && y, x && y) ||
        rewrite((x && (z || y)) && y, x && y) ||
        rewrite(y && (x && (y || z)), y && x) ||
        rewrite(y && (x && (z || y)), y && x) ||

        rewrite(((y || z) && x) && y, x && y) ||
        rewrite(((z || y) && x) && y, x && y) ||
        rewrite(y && ((y || z) && x), y && x) ||
        rewrite(y && ((z || y) && x), y && x) ||

        rewrite((x || y) && (x || z), x || (y && z)) ||
        rewrite((x || y) && (z || x), x || (y && z)) ||
        rewrite((y || x) && (x || z), x || (y && z)) ||
        rewrite((y || x) && (z || x), x || (y && z)) ||

        rewrite(x < y && x < z, x < min(y, z)) ||
        rewrite(y < x && z < x, max(y, z) < x) ||
        rewrite(x <= y && x <= z, x <= min(y, z)) ||
        rewrite(y <= x && z <= x, max(y, z) <= x)) {

        return mutate(rewrite.result, bounds);
    }

    if (a.same_as(op->a) &&
        b.same_as(op->b)) {
        return op;
    } else {
        return And::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
