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

    // clang-format off
    if (EVAL_IN_LAMBDA
        ((rewrite(x || true, b, "or22")) ||
         (rewrite(x || false, a, "or23")) ||
         (rewrite(x || x, a, "or24")) ||

         (rewrite((x || y) || x, a, "or26")) ||
         (rewrite(x || (x || y), b, "or27")) ||
         (rewrite((x || y) || y, a, "or28")) ||
         (rewrite(y || (x || y), b, "or29")) ||

         (rewrite(((x || y) || z) || x, a, "or31")) ||
         (rewrite(x || ((x || y) || z), b, "or32")) ||
         (rewrite((z || (x || y)) || x, a, "or33")) ||
         (rewrite(x || (z || (x || y)), b, "or34")) ||
         (rewrite(((x || y) || z) || y, a, "or35")) ||
         (rewrite(y || ((x || y) || z), b, "or36")) ||
         (rewrite((z || (x || y)) || y, a, "or37")) ||
         (rewrite(y || (z || (x || y)), b, "or38")) ||

         (rewrite((x && y) || x, b, "or40")) ||
         (rewrite(x || (x && y), a, "or41")) ||
         (rewrite((x && y) || y, b, "or42")) ||
         (rewrite(y || (x && y), a, "or43")) ||

         (rewrite(((x || y) || z) || x, a, "or45")) ||
         (rewrite(x || ((x || y) || z), b, "or46")) ||
         (rewrite((z || (x || y)) || x, a, "or47")) ||
         (rewrite(x || (z || (x || y)), b, "or48")) ||
         (rewrite(((x || y) || z) || y, a, "or49")) ||
         (rewrite(y || ((x || y) || z), b, "or50")) ||
         (rewrite((z || (x || y)) || y, a, "or51")) ||
         (rewrite(y || (z || (x || y)), b, "or52")) ||

         (rewrite(x != y || x == y, true, "or54")) ||
         (rewrite(x != y || y == x, true, "or55")) ||

         (rewrite((z || x != y) || x == y, true, "or56")) ||
         (rewrite((z || x != y) || y == x, true, "or57")) ||
         (rewrite((x != y || z) || x == y, true, "or58")) ||
         (rewrite((x != y || z) || y == x, true, "or59")) ||
         (rewrite((z || x == y) || x != y, true, "or60")) ||
         (rewrite((z || x == y) || y != x, true, "or61")) ||
         (rewrite((x == y || z) || x != y, true, "or62")) ||
         (rewrite((x == y || z) || y != x, true, "or63")) ||

         (rewrite(x || !x, true, "or64")) ||
         (rewrite(!x || x, true, "or65")) ||
         (rewrite(y <= x || x < y, true, "or66")) ||
         (rewrite(x != c0 || x == c1, a, c0 != c1, "or67")) ||
         (rewrite(x <= c0 || c1 <= x, true, !is_float(x) && c1 <= c0 + 1, "or68")) ||
         (rewrite(c1 <= x || x <= c0, true, !is_float(x) && c1 <= c0 + 1, "or69")) ||
         (rewrite(x <= c0 || c1 < x, true, c1 <= c0, "or70")) ||
         (rewrite(c1 <= x || x < c0, true, c1 <= c0, "or71")) ||
         (rewrite(x < c0 || c1 < x, true, c1 < c0, "or72")) ||
         (rewrite(c1 < x || x < c0, true, c1 < c0, "or73")) ||
         (rewrite(c0 < x || c1 < x, fold(min(c0, c1)) < x, "or74")) ||
         (rewrite(c0 <= x || c1 <= x, fold(min(c0, c1)) <= x, "or75")) ||
         (rewrite(x < c0 || x < c1, x < fold(max(c0, c1)), "or76")) ||
         (rewrite(x <= c0 || x <= c1, x <= fold(max(c0, c1)), "or77")))) {
        return rewrite.result;
    }
    // clang-format on

    if ((rewrite(broadcast(x) || broadcast(y), broadcast(x || y, op->type.lanes()), "or82")) ||

        (rewrite((x && (y || z)) || y, (x && z) || y, "or84")) ||
        (rewrite((x && (z || y)) || y, (x && z) || y, "or85")) ||
        (rewrite(y || (x && (y || z)), y || (x && z), "or86")) ||
        (rewrite(y || (x && (z || y)), y || (x && z), "or87")) ||

        (rewrite(((y || z) && x) || y, (z && x) || y, "or89")) ||
        (rewrite(((z || y) && x) || y, (z && x) || y, "or90")) ||
        (rewrite(y || ((y || z) && x), y || (z && x), "or91")) ||
        (rewrite(y || ((z || y) && x), y || (z && x), "or92")) ||

        (rewrite((x || (y && z)) || y, x || y, "or94")) ||
        (rewrite((x || (z && y)) || y, x || y, "or95")) ||
        (rewrite(y || (x || (y && z)), y || x, "or96")) ||
        (rewrite(y || (x || (z && y)), y || x, "or97")) ||

        (rewrite(((y && z) || x) || y, x || y, "or99")) ||
        (rewrite(((z && y) || x) || y, x || y, "or100")) ||
        (rewrite(y || ((y && z) || x), y || x, "or101")) ||
        (rewrite(y || ((z && y) || x), y || x, "or102")) ||

        (rewrite((x && y) || (x && z), x && (y || z), "or104")) ||
        (rewrite((x && y) || (z && x), x && (y || z), "or105")) ||
        (rewrite((y && x) || (x && z), x && (y || z), "or106")) ||
        (rewrite((y && x) || (z && x), x && (y || z), "or107")) ||

        (rewrite(x < y || x < z, x < max(y, z), "or109")) ||
        (rewrite(y < x || z < x, min(y, z) < x, "or110")) ||
        (rewrite(x <= y || x <= z, x <= max(y, z), "or111")) ||
        (rewrite(y <= x || z <= x, min(y, z) <= x, "or112"))) {

        return mutate(std::move(rewrite.result), bounds);
    }

    if (a.same_as(op->a) &&
        b.same_as(op->b)) {
        return op;
    } else {
        return Or::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide