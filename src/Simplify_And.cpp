#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const And *op, ExprInfo *info) {
    if (falsehoods.count(op)) {
        return const_false(op->type.lanes(), info);
    }

    Expr a = mutate(op->a, nullptr);
    Expr b = mutate(op->b, nullptr);

    // Order commutative operations by node type
    if (should_commute(a, b)) {
        std::swap(a, b);
    }

    if (info) {
        info->cast_to(op->type);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::and_op(a, b), op->type);

    // clang-format off

    // Cases that fold to a constant
    if (EVAL_IN_LAMBDA
        (rewrite(x && false, false) ||
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

         rewrite((!x && x), false) ||
         rewrite((x && !x), false) ||
         rewrite((!x && (x && y)), false) ||
         rewrite((!x && (y && x)), false) ||
         rewrite((x && !(x || y)), false) ||
         rewrite((x && (!x && y)), false) ||
         rewrite((x && (y && !x)), false) ||
         rewrite((!(x || y) && x), false) ||
         rewrite(((!x && y) && x), false) ||
         rewrite(((y && !x) && x), false) ||
         rewrite((!x && ((x && z) && y)), false) ||
         rewrite((!x && (y && (x && z))), false) ||
         rewrite((!x && ((z && x) && y)), false) ||
         rewrite((!x && (y && (z && x))), false) ||
         rewrite((x && !((x || z) || y)), false) ||
         rewrite((x && (!(x || z) && y)), false) ||
         rewrite((x && (y && !(x || z))), false) ||
         rewrite((x && (!(z || x) && y)), false) ||
         rewrite((x && (y && !(z || x))), false) ||
         rewrite((x && ((!x && z) && y)), false) ||
         rewrite((x && (y && (!x && z))), false) ||
         rewrite((!x && (!(!x || z) && y)), false) ||
         rewrite((!x && (y && !(!x || z))), false) ||
         rewrite((x && ((z && !x) && y)), false) ||
         rewrite((x && (y && (z && !x))), false) ||
         rewrite((!x && (!(z || !x) && y)), false) ||
         rewrite((!x && (y && !(z || !x))), false) ||
         rewrite(((x && y) && (!x && z)), false) ||
         rewrite(((x && y) && (z && !x)), false) ||
         rewrite(((y && x) && (!x && z)), false) ||
         rewrite(((y && x) && (z && !x)), false) ||
         rewrite((!(x || y) && (x && z)), false) ||
         rewrite((!(x || y) && (z && x)), false) ||
         rewrite(((!x && y) && (x && z)), false) ||
         rewrite(((!x && y) && (z && x)), false) ||
         rewrite(((y && !x) && (x && z)), false) ||
         rewrite(((y && !x) && (z && x)), false) ||
         rewrite((!((x || z) || y) && x), false) ||
         rewrite(((!(x || z) && y) && x), false) ||
         rewrite(((y && !(x || z)) && x), false) ||
         rewrite(((!(z || x) && y) && x), false) ||
         rewrite(((y && !(z || x)) && x), false) ||
         rewrite((((!x && z) && y) && x), false) ||
         rewrite(((y && (!x && z)) && x), false) ||
         rewrite((((z && !x) && y) && x), false) ||
         rewrite(((y && (z && !x)) && x), false) ||

         rewrite(y <= x && x < y, false) ||
         rewrite(y < x && x < y, false) ||
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
         rewrite(x <= c1 && c0 <= x, false, c1 < c0))) {
        set_expr_info_to_constant(info, false);
        return rewrite.result;
    }

    // Cases that fold to one of the args
    if (EVAL_IN_LAMBDA
        (rewrite(x && true, a) ||

         rewrite((x && x), a) ||
         rewrite((x && (x && y)), b) ||
         rewrite((x && (y && x)), b) ||
         rewrite((x && (x || y)), a) ||
         rewrite((x && (y || x)), a) ||
         rewrite((!x && (!x && y)), b) ||
         rewrite((!x && (!x || y)), a) ||
         rewrite((!x && (y && !x)), b) ||
         rewrite((!x && (y || !x)), a) ||
         rewrite(((x && y) && x), a) ||
         rewrite(((y && x) && x), a) ||
         rewrite(((x || y) && x), b) ||
         rewrite(((y || x) && x), b) ||
         rewrite((x && ((x && z) && y)), b) ||
         rewrite((x && (y && (x && z))), b) ||
         rewrite((!x && (!(x && z) || y)), a) ||
         rewrite((!x && (y || !(x && z))), a) ||
         rewrite((x && ((z && x) && y)), b) ||
         rewrite((x && (y && (z && x))), b) ||
         rewrite((!x && (!(z && x) || y)), a) ||
         rewrite((!x && (y || !(z && x))), a) ||
         rewrite((x && ((x || z) || y)), a) ||
         rewrite((x && (y || (x || z))), a) ||
         rewrite((!x && (!(x || z) && y)), b) ||
         rewrite((!x && (y && !(x || z))), b) ||
         rewrite((x && ((z || x) || y)), a) ||
         rewrite((x && (y || (z || x))), a) ||
         rewrite((!x && (!(z || x) && y)), b) ||
         rewrite((!x && (y && !(z || x))), b) ||
         rewrite((!x && ((!x && z) && y)), b) ||
         rewrite((x && !((!x && z) && y)), a) ||
         rewrite((!x && (y && (!x && z))), b) ||
         rewrite((x && (!(!x && z) || y)), a) ||
         rewrite((x && (y || !(!x && z))), a) ||
         rewrite((!x && ((!x || z) || y)), a) ||
         rewrite((x && !((!x || z) || y)), b) ||
         rewrite((!x && (y || (!x || z))), a) ||
         rewrite((x && (!(!x || z) && y)), b) ||
         rewrite((x && (y && !(!x || z))), b) ||
         rewrite((!x && ((z && !x) && y)), b) ||
         rewrite((!x && (y && (z && !x))), b) ||
         rewrite((x && (!(z && !x) || y)), a) ||
         rewrite((x && (y || !(z && !x))), a) ||
         rewrite((!x && ((z || !x) || y)), a) ||
         rewrite((!x && (y || (z || !x))), a) ||
         rewrite((x && (!(z || !x) && y)), b) ||
         rewrite((x && (y && !(z || !x))), b) ||
         rewrite((!(x && y) && (!x && z)), b) ||
         rewrite((!(x && y) && (z && !x)), b) ||
         rewrite(((x || y) && (x && z)), b) ||
         rewrite(((x || y) && (z && x)), b) ||
         rewrite((!(x || y) && (!x || z)), a) ||
         rewrite((!(x || y) && (z || !x)), a) ||
         rewrite(((y || x) && (x && z)), b) ||
         rewrite(((y || x) && (z && x)), b) ||
         rewrite(((!x || y) && (!x && z)), b) ||
         rewrite(((!x || y) && (z && !x)), b) ||
         rewrite(((y || !x) && (!x && z)), b) ||
         rewrite(((y || !x) && (z && !x)), b) ||
         rewrite((((x && z) && y) && x), a) ||
         rewrite(((y && (x && z)) && x), a) ||
         rewrite((((z && x) && y) && x), a) ||
         rewrite(((y && (z && x)) && x), a) ||
         rewrite((((x || z) || y) && x), b) ||
         rewrite(((y || (x || z)) && x), b) ||
         rewrite((((z || x) || y) && x), b) ||
         rewrite(((y || (z || x)) && x), b) ||
         rewrite((!((!x && z) && y) && x), b) ||
         rewrite(((!(!x && z) || y) && x), b) ||
         rewrite(((y || !(!x && z)) && x), b) ||
         rewrite((!((!x || z) || y) && x), a) ||
         rewrite(((!(!x || z) && y) && x), a) ||
         rewrite(((y && !(!x || z)) && x), a) ||
         rewrite(((!(z && !x) || y) && x), b) ||
         rewrite(((y || !(z && !x)) && x), b) ||
         rewrite(((!(z || !x) && y) && x), a) ||
         rewrite(((y && !(z || !x)) && x), a) ||

         rewrite(x != c0 && x == c1, b, c0 != c1) ||
         rewrite(c0 < x && c1 < x, fold(max(c0, c1)) < x) ||
         rewrite(c0 <= x && c1 <= x, fold(max(c0, c1)) <= x) ||
         rewrite(x < c0 && x < c1, x < fold(min(c0, c1))) ||
         rewrite(x <= c0 && x < c1, a, c0 < c1) ||
         rewrite(x <= c0 && x < c1, b, c1 <= c0) ||
         rewrite(x <= c0 && x <= c1, x <= fold(min(c0, c1))) ||
         false)) {
        return rewrite.result;
    }

    if (EVAL_IN_LAMBDA
        (rewrite(broadcast(x, c0) && broadcast(y, c0), broadcast(x && y, c0)) ||
         rewrite((x && broadcast(y, c0)) && broadcast(z, c0), x && broadcast(y && z, c0)) ||
         rewrite((broadcast(x, c0) && y) && broadcast(z, c0), broadcast(x && z, c0) && y) ||

         rewrite(!x && !y, !(x || y)) ||

         rewrite((x && !(x && y)), (!y && x)) ||
         rewrite((!x && (x || y)), (!x && y)) ||
         rewrite((!x && (y || x)), (!x && y)) ||
         rewrite((x && (!x || y)), (x && y)) ||
         rewrite((x && (y || !x)), (x && y)) ||
         rewrite((!(x && y) && x), (!y && x)) ||
         rewrite(((!x || y) && x), (x && y)) ||
         rewrite(((y || !x) && x), (x && y)) ||
         rewrite((x && !((x && z) && y)), ((!y || !z) && x)) ||
         rewrite((x && ((x && z) || y)), ((y || z) && x)) ||
         rewrite((!x && ((x && z) || y)), (!x && y)) ||
         rewrite((x && !((x && z) || y)), (!(y || z) && x)) ||
         rewrite((x && (y || (x && z))), ((y || z) && x)) ||
         rewrite((!x && (y || (x && z))), (!x && y)) ||
         rewrite((x && (!(x && z) && y)), ((!z && y) && x)) ||
         rewrite((!x && (!(x && z) && y)), (!x && y)) ||
         rewrite((x && (!(x && z) || y)), ((!z || y) && x)) ||
         rewrite((x && (y && !(x && z))), ((!z && y) && x)) ||
         rewrite((!x && (y && !(x && z))), (!x && y)) ||
         rewrite((x && (y || !(x && z))), ((!z || y) && x)) ||
         rewrite((x && ((z && x) || y)), ((y || z) && x)) ||
         rewrite((!x && ((z && x) || y)), (!x && y)) ||
         rewrite((x && (y || (z && x))), ((y || z) && x)) ||
         rewrite((!x && (y || (z && x))), (!x && y)) ||
         rewrite((x && (!(z && x) && y)), ((!z && y) && x)) ||
         rewrite((!x && (!(z && x) && y)), (!x && y)) ||
         rewrite((x && (!(z && x) || y)), ((!z || y) && x)) ||
         rewrite((x && (y && !(z && x))), ((!z && y) && x)) ||
         rewrite((!x && (y && !(z && x))), (!x && y)) ||
         rewrite((x && (y || !(z && x))), ((!z || y) && x)) ||
         rewrite((x && ((x || z) && y)), (x && y)) ||
         rewrite((!x && ((x || z) && y)), (!x && (y && z))) ||
         rewrite((x && !((x || z) && y)), (!y && x)) ||
         rewrite((x && (y && (x || z))), (x && y)) ||
         rewrite((!x && (y && (x || z))), (!x && (y && z))) ||
         rewrite((!x && ((x || z) || y)), (!x && (y || z))) ||
         rewrite((!x && (y || (x || z))), (!x && (y || z))) ||
         rewrite((x && (!(x || z) || y)), (x && y)) ||
         rewrite((!x && (!(x || z) || y)), (!x && (!z || y))) ||
         rewrite((x && (y || !(x || z))), (x && y)) ||
         rewrite((!x && (y || !(x || z))), (!x && (!z || y))) ||
         rewrite((x && ((z || x) && y)), (x && y)) ||
         rewrite((!x && ((z || x) && y)), (!x && (y && z))) ||
         rewrite((x && (y && (z || x))), (x && y)) ||
         rewrite((!x && (y && (z || x))), (!x && (y && z))) ||
         rewrite((!x && ((z || x) || y)), (!x && (y || z))) ||
         rewrite((!x && (y || (z || x))), (!x && (y || z))) ||
         rewrite((x && (!(z || x) || y)), (x && y)) ||
         rewrite((!x && (!(z || x) || y)), (!x && (!z || y))) ||
         rewrite((x && (y || !(z || x))), (x && y)) ||
         rewrite((!x && (y || !(z || x))), (!x && (!z || y))) ||
         rewrite((x && ((!x && z) || y)), (x && y)) ||
         rewrite((!x && ((!x && z) || y)), (!x && (y || z))) ||
         rewrite((x && !((!x && z) || y)), (!y && x)) ||
         rewrite((x && (y || (!x && z))), (x && y)) ||
         rewrite((!x && (y || (!x && z))), (!x && (y || z))) ||
         rewrite((x && (!(!x && z) && y)), (x && y)) ||
         rewrite((!x && (!(!x && z) && y)), (!x && (!z && y))) ||
         rewrite((!x && (!(!x && z) || y)), (!x && (!z || y))) ||
         rewrite((x && (y && !(!x && z))), (x && y)) ||
         rewrite((!x && (y && !(!x && z))), (!x && (!z && y))) ||
         rewrite((!x && (y || !(!x && z))), (!x && (!z || y))) ||
         rewrite((x && ((!x || z) && y)), ((y && z) && x)) ||
         rewrite((!x && ((!x || z) && y)), (!x && y)) ||
         rewrite((x && !((!x || z) && y)), ((!y || !z) && x)) ||
         rewrite((x && (y && (!x || z))), ((y && z) && x)) ||
         rewrite((!x && (y && (!x || z))), (!x && y)) ||
         rewrite((x && ((!x || z) || y)), ((y || z) && x)) ||
         rewrite((x && (y || (!x || z))), ((y || z) && x)) ||
         rewrite((x && (!(!x || z) || y)), ((!z || y) && x)) ||
         rewrite((!x && (!(!x || z) || y)), (!x && y)) ||
         rewrite((x && (y || !(!x || z))), ((!z || y) && x)) ||
         rewrite((!x && (y || !(!x || z))), (!x && y)) ||
         rewrite((x && ((z && !x) || y)), (x && y)) ||
         rewrite((!x && ((z && !x) || y)), (!x && (y || z))) ||
         rewrite((x && (y || (z && !x))), (x && y)) ||
         rewrite((!x && (y || (z && !x))), (!x && (y || z))) ||
         rewrite((x && (!(z && !x) && y)), (x && y)) ||
         rewrite((!x && (!(z && !x) && y)), (!x && (!z && y))) ||
         rewrite((!x && (!(z && !x) || y)), (!x && (!z || y))) ||
         rewrite((x && (y && !(z && !x))), (x && y)) ||
         rewrite((!x && (y && !(z && !x))), (!x && (!z && y))) ||
         rewrite((!x && (y || !(z && !x))), (!x && (!z || y))) ||
         rewrite((x && ((z || !x) && y)), ((y && z) && x)) ||
         rewrite((!x && ((z || !x) && y)), (!x && y)) ||
         rewrite((x && (y && (z || !x))), ((y && z) && x)) ||
         rewrite((!x && (y && (z || !x))), (!x && y)) ||
         rewrite((x && ((z || !x) || y)), ((y || z) && x)) ||
         rewrite((x && (y || (z || !x))), ((y || z) && x)) ||
         rewrite((x && (!(z || !x) || y)), ((!z || y) && x)) ||
         rewrite((!x && (!(z || !x) || y)), (!x && y)) ||
         rewrite((x && (y || !(z || !x))), ((!z || y) && x)) ||
         rewrite((!x && (y || !(z || !x))), (!x && y)) ||
         rewrite(((x && y) && (x && z)), ((y && z) && x)) ||
         rewrite((!(x && y) && (x && z)), ((!y && z) && x)) ||
         rewrite(((x && y) && (z && x)), ((y && z) && x)) ||
         rewrite((!(x && y) && (z && x)), ((!y && z) && x)) ||
         rewrite((!(x && y) && (x || z)), select(x, !y, z)) ||
         rewrite((!(x && y) && (z || x)), select(x, !y, z)) ||
         rewrite((!(x && y) && (!x || z)), (!x || (!y && z))) ||
         rewrite((!(x && y) && (z || !x)), (!x || (!y && z))) ||
         rewrite(((y && x) && (x && z)), ((y && z) && x)) ||
         rewrite(((y && x) && (z && x)), ((y && z) && x)) ||
         rewrite(((x || y) && (x || z)), ((y && z) || x)) ||
         rewrite((!(x || y) && (x || z)), (!x && (!y && z))) ||
         rewrite(((x || y) && (z || x)), ((y && z) || x)) ||
         rewrite((!(x || y) && (z || x)), (!x && (!y && z))) ||
         rewrite(((x || y) && (!x && z)), (!x && (y && z))) ||
         rewrite((!(x || y) && (!x && z)), (!x && (!y && z))) ||
         rewrite(((x || y) && (!x || z)), select(x, z, y)) ||
         rewrite(((x || y) && (z && !x)), (!x && (y && z))) ||
         rewrite((!(x || y) && (z && !x)), (!x && (!y && z))) ||
         rewrite(((x || y) && (z || !x)), select(x, z, y)) ||
         rewrite(((y || x) && (x || z)), ((y && z) || x)) ||
         rewrite(((y || x) && (z || x)), ((y && z) || x)) ||
         rewrite(((y || x) && (!x && z)), (!x && (y && z))) ||
         rewrite(((y || x) && (!x || z)), select(x, z, y)) ||
         rewrite(((y || x) && (z && !x)), (!x && (y && z))) ||
         rewrite(((y || x) && (z || !x)), select(x, z, y)) ||
         rewrite(((!x && y) && (!x && z)), (!x && (y && z))) ||
         rewrite(((!x && y) && (z && !x)), (!x && (y && z))) ||
         rewrite(((!x || y) && (x && z)), ((y && z) && x)) ||
         rewrite(((!x || y) && (z && x)), ((y && z) && x)) ||
         rewrite(((!x || y) && (x || z)), select(x, y, z)) ||
         rewrite(((!x || y) && (z || x)), select(x, y, z)) ||
         rewrite(((!x || y) && (!x || z)), (!x || (y && z))) ||
         rewrite(((!x || y) && (z || !x)), (!x || (y && z))) ||
         rewrite(((y && !x) && (!x && z)), (!x && (y && z))) ||
         rewrite(((y && !x) && (z && !x)), (!x && (y && z))) ||
         rewrite(((y || !x) && (x && z)), ((y && z) && x)) ||
         rewrite(((y || !x) && (z && x)), ((y && z) && x)) ||
         rewrite(((y || !x) && (x || z)), select(x, y, z)) ||
         rewrite(((y || !x) && (z || x)), select(x, y, z)) ||
         rewrite(((y || !x) && (!x || z)), (!x || (y && z))) ||
         rewrite(((y || !x) && (z || !x)), (!x || (y && z))) ||
         rewrite((!((x && z) && y) && x), ((!y || !z) && x)) ||
         rewrite((((x && z) || y) && x), ((y || z) && x)) ||
         rewrite((!((x && z) || y) && x), (!(y || z) && x)) ||
         rewrite(((y || (x && z)) && x), ((y || z) && x)) ||
         rewrite(((!(x && z) && y) && x), ((!z && y) && x)) ||
         rewrite(((!(x && z) || y) && x), ((!z || y) && x)) ||
         rewrite(((y && !(x && z)) && x), ((!z && y) && x)) ||
         rewrite(((y || !(x && z)) && x), ((!z || y) && x)) ||
         rewrite((((z && x) || y) && x), ((y || z) && x)) ||
         rewrite(((y || (z && x)) && x), ((y || z) && x)) ||
         rewrite(((!(z && x) && y) && x), ((!z && y) && x)) ||
         rewrite(((!(z && x) || y) && x), ((!z || y) && x)) ||
         rewrite(((y && !(z && x)) && x), ((!z && y) && x)) ||
         rewrite(((y || !(z && x)) && x), ((!z || y) && x)) ||
         rewrite((((x || z) && y) && x), (x && y)) ||
         rewrite((!((x || z) && y) && x), (!y && x)) ||
         rewrite(((y && (x || z)) && x), (x && y)) ||
         rewrite(((!(x || z) || y) && x), (x && y)) ||
         rewrite(((y || !(x || z)) && x), (x && y)) ||
         rewrite((((z || x) && y) && x), (x && y)) ||
         rewrite(((y && (z || x)) && x), (x && y)) ||
         rewrite(((!(z || x) || y) && x), (x && y)) ||
         rewrite(((y || !(z || x)) && x), (x && y)) ||
         rewrite((((!x && z) || y) && x), (x && y)) ||
         rewrite((!((!x && z) || y) && x), (!y && x)) ||
         rewrite(((y || (!x && z)) && x), (x && y)) ||
         rewrite(((!(!x && z) && y) && x), (x && y)) ||
         rewrite(((y && !(!x && z)) && x), (x && y)) ||
         rewrite((((!x || z) && y) && x), ((y && z) && x)) ||
         rewrite((!((!x || z) && y) && x), ((!y || !z) && x)) ||
         rewrite(((y && (!x || z)) && x), ((y && z) && x)) ||
         rewrite((((!x || z) || y) && x), ((y || z) && x)) ||
         rewrite(((y || (!x || z)) && x), ((y || z) && x)) ||
         rewrite(((!(!x || z) || y) && x), ((!z || y) && x)) ||
         rewrite(((y || !(!x || z)) && x), ((!z || y) && x)) ||
         rewrite((((z && !x) || y) && x), (x && y)) ||
         rewrite(((y || (z && !x)) && x), (x && y)) ||
         rewrite(((!(z && !x) && y) && x), (x && y)) ||
         rewrite(((y && !(z && !x)) && x), (x && y)) ||
         rewrite((((z || !x) && y) && x), ((y && z) && x)) ||
         rewrite(((y && (z || !x)) && x), ((y && z) && x)) ||
         rewrite((((z || !x) || y) && x), ((y || z) && x)) ||
         rewrite(((y || (z || !x)) && x), ((y || z) && x)) ||
         rewrite(((!(z || !x) || y) && x), ((!z || y) && x)) ||
         rewrite(((y || !(z || !x)) && x), ((!z || y) && x)) ||

         rewrite(x < y && x < z, x < min(y, z)) ||
         rewrite(y < x && z < x, max(y, z) < x) ||
         rewrite(x <= y && x <= z, x <= min(y, z)) ||
         rewrite(y <= x && z <= x, max(y, z) <= x) ||

         false)) {

        return mutate(rewrite.result, info);
    }
    // clang-format on

    if (a.same_as(op->a) &&
        b.same_as(op->b)) {
        return op;
    } else {
        return And::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
