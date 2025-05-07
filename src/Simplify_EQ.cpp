#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const EQ *op, ExprInfo *info) {
    if (info) {
        // There are three possibilities:
        // 1) We know the result is zero.
        // 2) We know the result is one.
        // 3) The result might be either zero or one.
        // The line below takes care of case 3, and cases 1 and 2 are handled in
        // the constant folding rules that come later in this method.
        info->cast_to(op->type);
    }

    if (truths.count(op)) {
        return const_true(op->type.lanes(), info);
    } else if (falsehoods.count(op)) {
        return const_false(op->type.lanes(), info);
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
        auto rewrite = IRMatcher::rewriter(IRMatcher::eq(a, b), op->type);
        if (rewrite(x == 1, x)) {
            return rewrite.result;
        } else if (rewrite(x == 0, !x)) {
            return mutate(rewrite.result, info);
        } else if (rewrite(x == x, true)) {
            return const_true(op->type.lanes(), info);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(a, b);
        }
    }

    ExprInfo delta_info;
    Expr delta = mutate(op->a - op->b, &delta_info);
    const int lanes = op->type.lanes();

    // If the delta is 0, then it's just x == x
    if (is_const_zero(delta)) {
        return const_true(lanes, info);
    }

    // Attempt to disprove using bounds analysis
    if (!delta_info.bounds.contains(0)) {
        return const_false(lanes, info);
    }

    // Attempt to disprove using modulus remainder analysis
    if (delta_info.alignment.remainder != 0) {
        return const_false(lanes, info);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::eq(delta, 0), op->type, delta.type());

    bool allowed_overflow = no_overflow(delta.type());

    if (rewrite(broadcast(x, c0) == 0, broadcast(x == 0, c0)) ||
        (allowed_overflow &&
         (rewrite(x * y == 0, (x == 0) || (y == 0)) ||
          rewrite(x * c0 + c1 == 0, x == fold((0 - c1) / c0), c1 % c0 == 0))) ||
        rewrite(select(x, 0, y) == 0, x || (y == 0)) ||
        rewrite(select(x, c0, y) == 0, !x && (y == 0), c0 != 0) ||
        rewrite(select(x, y, 0) == 0, !x || (y == 0)) ||
        rewrite(select(x, y, c0) == 0, x && (y == 0), c0 != 0) ||

        rewrite(select(x, c0, y) + c1 == 0, x || (y == fold(-c1)), c0 + c1 == 0) ||
        rewrite(select(x, y, c0) + c1 == 0, !x || (y == fold(-c1)), c0 + c1 == 0) ||
        rewrite(select(x, c0, y) + c1 == 0, !x && (y == fold(-c1)), c0 + c1 != 0) ||
        rewrite(select(x, y, c0) + c1 == 0, x && (y == fold(-c1)), c0 + c1 != 0) ||

        rewrite(max(x, y) - y == 0, x <= y) ||
        rewrite(min(x, y) - y == 0, y <= x) ||
        rewrite(max(y, x) - y == 0, x <= y) ||
        rewrite(min(y, x) - y == 0, y <= x) ||
        rewrite(y - max(x, y) == 0, x <= y) ||
        rewrite(y - min(x, y) == 0, y <= x) ||
        rewrite(y - max(y, x) == 0, x <= y) ||
        rewrite(y - min(y, x) == 0, y <= x) ||

        // Guard against `c0 + c1` overflowing.
        (allowed_overflow &&
         (rewrite(max(x, c0) + c1 == 0, x == fold(-c1), c0 + c1 < 0) ||
          rewrite(min(x, c0) + c1 == 0, x == fold(-c1), c0 + c1 > 0) ||
          rewrite(max(x, c0) + c1 == 0, false, c0 + c1 > 0) ||
          rewrite(min(x, c0) + c1 == 0, false, c0 + c1 < 0) ||
          rewrite(max(x, c0) + c1 == 0, x <= c0, c0 + c1 == 0) ||
          rewrite(min(x, c0) + c1 == 0, c0 <= x, c0 + c1 == 0))) ||

        // Special case the above where c1 == 0
        rewrite(max(x, c0) == 0, x == 0, c0 < 0) ||
        rewrite(min(x, c0) == 0, x == 0, c0 > 0) ||
        rewrite(max(x, c0) == 0, false, c0 > 0) ||
        rewrite(min(x, c0) == 0, false, c0 < 0) ||
        rewrite(max(x, 0) == 0, x <= 0) ||
        rewrite(min(x, 0) == 0, 0 <= x) ||

        false) {
        return mutate(rewrite.result, info);
    }

    if (rewrite(c0 == 0, fold(c0 == 0)) ||
        rewrite((x - y) + c0 == 0, x == y + fold(-c0)) ||
        rewrite(x + c0 == 0, x == fold(-c0)) ||
        rewrite(c0 - x == 0, x == c0) ||
        rewrite(x - y == 0, x == y) ||
        rewrite(x == 0, x == 0)) {
        const EQ *eq = rewrite.result.as<EQ>();
        if (eq &&
            eq->a.same_as(op->a) &&
            equal(eq->b, op->b)) {
            // Note we don't use same_as for b, because the shuffling of the RHS
            // to the LHS and back might mutate it and then mutate it back.
            return op;
        } else {
            return rewrite.result;
        }
    }

    // Unreachable. That last rewrite catches everything and
    // constructs delta == 0 for us.
    return Expr();
}

// ne redirects to not eq
Expr Simplify::visit(const NE *op, ExprInfo *info) {
    if (!may_simplify(op->a.type())) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return NE::make(a, b);
        }
    }

    Expr mutated = mutate(Not::make(EQ::make(op->a, op->b)), info);
    if (const NE *ne = mutated.as<NE>()) {
        if (ne->a.same_as(op->a) && ne->b.same_as(op->b)) {
            return op;
        }
    }
    return mutated;
}

}  // namespace Internal
}  // namespace Halide
