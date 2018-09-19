#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const And *op, ConstBounds *bounds) {
    if (falsehoods.count(op)) {
        return const_false(op->type.lanes());
    }

    // Exploit the assumed truth of the second side while mutating
    // the first. Then assume the mutated first side while
    // mutating the second.
    Expr a, b;
    {
        auto fact = scoped_truth(op->b);
        a = mutate(op->a, nullptr);
    }
    {
        // Note that we assume the *mutated* a here. The transformation
        // A && B == A && (B | A) is legal (where | means "given")
        // As is
        // A && B = (A | B) && B
        // But the transformation
        // A && B == (A | B) && (B | A) is not
        auto fact = scoped_truth(a);
        b = mutate(op->b, nullptr);
    }

    // Order commutative operations by node type
    if (should_commute(a, b)) {
        std::swap(a, b);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::and_op(a, b), op->type);

    if (EVAL_IN_LAMBDA
        (rewrite(x && true, a) ||
         rewrite(x && false, b) ||
         rewrite(x && x, a) ||
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

    if (rewrite(broadcast(x) && broadcast(y), broadcast(x && y, op->type.lanes()))) {
        return mutate(std::move(rewrite.result), bounds);
    }

    if (a.same_as(op->a) &&
        b.same_as(op->b)) {
        return op;
    } else {
        return And::make(a, b);
    }
}

}
}
