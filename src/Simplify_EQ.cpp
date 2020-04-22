#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const EQ *op, ExprInfo *bounds) {
    if (truths.count(op)) {
        return const_true(op->type.lanes());
    } else if (falsehoods.count(op)) {
        return const_false(op->type.lanes());
    }

    if (!may_simplify(op->a.type())) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(a, b);
        }
    }

    if (op->a.type().is_bool()) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        if (should_commute(a, b)) {
            std::swap(a, b);
        }
        const int lanes = op->type.lanes();
        auto rewrite = IRMatcher::rewriter(IRMatcher::eq(a, b), op->type);
        if (rewrite(x == 1, x, "eq28")) {
            return rewrite.result;
        } else if (rewrite(x == 0, !x, "eq30")) {
            return mutate(std::move(rewrite.result), bounds);
        } else if (rewrite(x == x, const_true(lanes), "eq32")) {
            return rewrite.result;
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(a, b);
        }
    }

    ExprInfo delta_bounds;
    Expr delta = mutate(op->a - op->b, &delta_bounds);
    const int lanes = op->type.lanes();

    // If the delta is 0, then it's just x == x
    if (is_zero(delta)) {
        return const_true(lanes);
    }

    // Attempt to disprove using bounds analysis
    if (delta_bounds.min_defined && delta_bounds.min > 0) {
        return const_false(lanes);
    }

    if (delta_bounds.max_defined && delta_bounds.max < 0) {
        return const_false(lanes);
    }

    // Attempt to disprove using modulus remainder analysis
    if (delta_bounds.alignment.remainder != 0) {
        return const_false(lanes);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::eq(delta, 0), op->type, delta.type());

    if ((rewrite(broadcast(x) == 0, broadcast(x == 0, lanes), "eq66")) ||
        (no_overflow(delta.type()) && (rewrite(x * y == 0, (x == 0) || (y == 0), "eq67"))) ||
        (rewrite(select(x, 0, y) == 0, x || (y == 0), "eq68")) ||
        (rewrite(select(x, c0, y) == 0, !x && (y == 0), c0 != 0, "eq69")) ||
        (rewrite(select(x, y, 0) == 0, !x || (y == 0), "eq70")) ||
        (rewrite(select(x, y, c0) == 0, x && (y == 0), c0 != 0, "eq71")) ||

        (rewrite(select(x, c0, y) + c1 == 0, x || (y == fold(-c1)), c0 + c1 == 0, "eq103")) ||
        (rewrite(select(x, y, c0) + c1 == 0, !x || (y == fold(-c1)), c0 + c1 == 0, "eq104")) ||
        (rewrite(select(x, c0, y) + c1 == 0, !x && (y == fold(-c1)), c0 + c1 != 0, "eq105")) ||
        (rewrite(select(x, y, c0) + c1 == 0, x && (y == fold(-c1)), c0 + c1 != 0, "eq106")) ||

        (rewrite(max(x, y) - y == 0, x <= y, "eq72")) ||
        (rewrite(min(x, y) - y == 0, y <= x, "eq73")) ||
        (rewrite(max(y, x) - y == 0, x <= y, "eq74")) ||
        (rewrite(min(y, x) - y == 0, y <= x, "eq75")) ||
        (rewrite(y - max(x, y) == 0, x <= y, "eq76")) ||
        (rewrite(y - min(x, y) == 0, y <= x, "eq77")) ||
        (rewrite(y - max(y, x) == 0, x <= y, "eq78")) ||
        (rewrite(y - min(y, x) == 0, y <= x, "eq79")) ||
        (rewrite(max(x, c0) + c1 == 0, x == fold(-c1), c0 + c1 < 0, "eq80")) ||
        (rewrite(min(x, c0) + c1 == 0, x == fold(-c1), c0 + c1 > 0, "eq81")) ||
        (rewrite(max(x, c0) + c1 == 0, false, c0 + c1 > 0, "eq82")) ||
        (rewrite(min(x, c0) + c1 == 0, false, c0 + c1 < 0, "eq83")) ||
        (rewrite(max(x, c0) + c1 == 0, x <= c0, c0 + c1 == 0, "eq84")) ||
        (rewrite(min(x, c0) + c1 == 0, c0 <= x, c0 + c1 == 0, "eq85")) ||

        // Special case the above where c1 == 0
        (rewrite(max(x, c0) == 0, x == 0, c0 < 0, "eq87")) ||
        (rewrite(min(x, c0) == 0, x == 0, c0 > 0, "eq88")) ||
        (rewrite(max(x, c0) == 0, false, c0 > 0, "eq89")) ||
        (rewrite(min(x, c0) == 0, false, c0 < 0, "eq90")) ||
        (rewrite(max(x, 0) == 0, x <= 0, "eq91")) ||
        (rewrite(min(x, 0) == 0, 0 <= x, "eq92")) ||

        false) {

        return mutate(std::move(rewrite.result), bounds);
    }

    if ((rewrite(c0 == 0, fold(c0 == 0), "eq99")) ||
        (rewrite((x - y) + c0 == 0, x == y + fold(-c0), "eq100")) ||
        (rewrite(x + c0 == 0, x == fold(-c0), "eq101")) ||
        (rewrite(c0 - x == 0, x == c0, "eq102"))) {
        return rewrite.result;
    }

    if (const Sub *s = delta.as<Sub>()) {
        Expr a = s->a, b = s->b;
        if (should_commute(a, b)) {
            std::swap(a, b);
        }
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(a, b);
        }
    }

    return delta == make_zero(op->a.type());
}

// ne redirects to not eq
Expr Simplify::visit(const NE *op, ExprInfo *bounds) {
    if (!may_simplify(op->a.type())) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return NE::make(a, b);
        }
    }

    Expr mutated = mutate(Not::make(EQ::make(op->a, op->b)), bounds);
    if (const NE *ne = mutated.as<NE>()) {
        if (ne->a.same_as(op->a) && ne->b.same_as(op->b)) {
            return op;
        }
    }
    return mutated;
}

}  // namespace Internal
}  // namespace Halide