#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Or *op, ConstBounds *bounds) {
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

    if (rewrite(broadcast(x) || broadcast(y), broadcast(x || y, op->type.lanes()))) {
        return mutate(std::move(rewrite.result), bounds);
    }

    if (a.same_as(op->a) &&
        b.same_as(op->b)) {
        return op;
    } else {
        return Or::make(a, b);
    }
}

}
}
