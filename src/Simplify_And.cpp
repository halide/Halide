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
        ((rewrite(x && true, x, "and23")) ||
         (rewrite(x && false, false, "and24")) ||
         (rewrite(x && x, x, "and25")) ||

         (rewrite((x && y) && x, (x && y), "and27")) ||
         (rewrite(x && (x && y), (x && y), "and28")) ||
         (rewrite((x && y) && y, (x && y), "and29")) ||
         (rewrite(y && (x && y), (x && y), "and30")) ||

         (rewrite(((x && y) && z) && x, ((x && y) && z), "and32")) ||
         (rewrite(x && ((x && y) && z), ((x && y) && z), "and33")) ||
         (rewrite((z && (x && y)) && x, (z && (x && y)), "and34")) ||
         (rewrite(x && (z && (x && y)), (z && (x && y)), "and35")) ||
         (rewrite(((x && y) && z) && y, ((x && y) && z), "and36")) ||
         (rewrite(y && ((x && y) && z), ((x && y) && z), "and37")) ||
         (rewrite((z && (x && y)) && y, (z && (x && y)), "and38")) ||
         (rewrite(y && (z && (x && y)), (z && (x && y)), "and39")) ||

         (rewrite((x || y) && x, x, "and41")) ||
         (rewrite(x && (x || y), x, "and42")) ||
         (rewrite((x || y) && y, y, "and43")) ||
         (rewrite(y && (x || y), y, "and44")) ||

         (rewrite(x != y && x == y, false, "and46")) ||
         (rewrite(x != y && y == x, false, "and47")) ||
         (rewrite((z && x != y) && x == y, false, "and48")) ||
         (rewrite((z && x != y) && y == x, false, "and49")) ||
         (rewrite((x != y && z) && x == y, false, "and50")) ||
         (rewrite((x != y && z) && y == x, false, "and51")) ||
         (rewrite((z && x == y) && x != y, false, "and52")) ||
         (rewrite((z && x == y) && y != x, false, "and53")) ||
         (rewrite((x == y && z) && x != y, false, "and54")) ||
         (rewrite((x == y && z) && y != x, false, "and55")) ||
         (rewrite(x && !x, false, "and56")) ||
         (rewrite(!x && x, false, "and57")) ||
         (rewrite(y <= x && x < y, false, "and58")) ||
         (rewrite(x != c0 && x == c1, x == c1, c0 != c1, "and59")) ||
         // Note: In the predicate below, if undefined overflow
         // occurs, the predicate counts as false. If well-defined
         // overflow occurs, the condition couldn't possibly
         // trigger because c0 + 1 will be the smallest possible
         // value.
         (rewrite(c0 < x && x < c1, false, !is_float(x) && c1 <= c0 + 1, "and65")) ||
         (rewrite(x < c1 && c0 < x, false, !is_float(x) && c1 <= c0 + 1, "and66")) ||
         (rewrite(x <= c1 && c0 < x, false, c1 <= c0, "and67")) ||
         (rewrite(c0 <= x && x < c1, false, c1 <= c0, "and68")) ||
         (rewrite(c0 <= x && x <= c1, false, c1 < c0, "and69")) ||
         (rewrite(x <= c1 && c0 <= x, false, c1 < c0, "and70")) ||
         (rewrite(c0 < x && c1 < x, fold(max(c0, c1)) < x, "and71")) ||
         (rewrite(c0 <= x && c1 <= x, fold(max(c0, c1)) <= x, "and72")) ||
         (rewrite(x < c0 && x < c1, x < fold(min(c0, c1)), "and73")) ||
         (rewrite(x <= c0 && x <= c1, x <= fold(min(c0, c1)), "and74")))) {
        return rewrite.result;
    }
    // clang-format on

    if ((rewrite(broadcast(x) && broadcast(y), broadcast(x && y, op->type.lanes()), "and79")) ||

        (rewrite((x || (y && z)) && y, (x || z) && y, "and81")) ||
        (rewrite((x || (z && y)) && y, (x || z) && y, "and82")) ||
        (rewrite(y && (x || (y && z)), y && (x || z), "and83")) ||
        (rewrite(y && (x || (z && y)), y && (x || z), "and84")) ||

        (rewrite(((y && z) || x) && y, (z || x) && y, "and86")) ||
        (rewrite(((z && y) || x) && y, (z || x) && y, "and87")) ||
        (rewrite(y && ((y && z) || x), y && (z || x), "and88")) ||
        (rewrite(y && ((z && y) || x), y && (z || x), "and89")) ||

        (rewrite((x && (y || z)) && y, x && y, "and91")) ||
        (rewrite((x && (z || y)) && y, x && y, "and92")) ||
        (rewrite(y && (x && (y || z)), y && x, "and93")) ||
        (rewrite(y && (x && (z || y)), y && x, "and94")) ||

        (rewrite(((y || z) && x) && y, x && y, "and96")) ||
        (rewrite(((z || y) && x) && y, x && y, "and97")) ||
        (rewrite(y && ((y || z) && x), y && x, "and98")) ||
        (rewrite(y && ((z || y) && x), y && x, "and99")) ||

        (rewrite((x || y) && (x || z), x || (y && z), "and101")) ||
        (rewrite((x || y) && (z || x), x || (y && z), "and102")) ||
        (rewrite((y || x) && (x || z), x || (y && z), "and103")) ||
        (rewrite((y || x) && (z || x), x || (y && z), "and104")) ||

        (rewrite(x < y && x < z, x < min(y, z), "and106")) ||
        (rewrite(y < x && z < x, max(y, z) < x, "and107")) ||
        (rewrite(x <= y && x <= z, x <= min(y, z), "and108")) ||
        (rewrite(y <= x && z <= x, max(y, z) <= x, "and109")) ||
        (rewrite(x <= y && y <= x, x == y, "and110")) ||
        false) {

        return mutate(std::move(rewrite.result), bounds);
    }

    if (use_synthesized_rules &&
        (
#include "Simplify_And.inc"
            )) {
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
