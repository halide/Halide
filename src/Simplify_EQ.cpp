#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const EQ *op, ExprInfo *bounds) {
    if (truths.count(op)) {
        return const_true(op->type.lanes());
    } else if (falsehoods.count(op)) {
        return const_false(op->type.lanes());
    }

    // Order commutative operations by node type
    Expr a = op->a;
    Expr b = op->b;
    if (should_commute(a, b)) {
        std::swap(a, b);
    }

    if (!may_simplify(a.type())) {
        a = mutate(a, nullptr);
        b = mutate(b, nullptr);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(a, b);
        }
    }

    if (op->a.type().is_bool()) {
        a = mutate(a, nullptr);
        b = mutate(b, nullptr);
        const int lanes = op->type.lanes();
        auto rewrite = IRMatcher::rewriter(IRMatcher::eq(a, b), op->type);
        if (rewrite(x == 1, x)) {
            return rewrite.result;
        } else if (rewrite(x == 0, !x)) {
            return mutate(std::move(rewrite.result), bounds);
        } else if (rewrite(x == x, const_true(lanes))) {
            return rewrite.result;
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(a, b);
        }
    }

    ExprInfo delta_bounds;
    Expr delta = mutate(a - b, &delta_bounds);
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

    if (
        #ifdef EXCLUDE_INVALID_ORDERING_RULES
        rewrite(broadcast(x) == 0, broadcast(x == 0, lanes)) ||
        #endif
        (no_overflow(delta.type()) && rewrite(x * y == 0, (x == 0) || (y == 0))) ||
        rewrite(select(x, 0, y) == 0, x || (y == 0)) ||
        #ifdef EXCLUDE_INVALID_ORDERING_RULES
        rewrite(select(x, c0, y) == 0, !x && (y == 0), c0 != 0) ||
        rewrite(select(x, y, 0) == 0, !x || (y == 0)) ||
        #endif
        rewrite(select(x, y, c0) == 0, x && (y == 0), c0 != 0) ||
        rewrite(max(x, y) - y == 0, x <= y) ||
        rewrite(min(x, y) - y == 0, y <= x) ||
        rewrite(max(y, x) - y == 0, x <= y) ||
        rewrite(min(y, x) - y == 0, y <= x) ||
        rewrite(y - max(x, y) == 0, x <= y) ||
        rewrite(y - min(x, y) == 0, y <= x) ||
        rewrite(y - max(y, x) == 0, x <= y) ||
        rewrite(y - min(y, x) == 0, y <= x) ||
        rewrite(max(x, c0) + c1 == 0, x == fold(-c1), c0 + c1 < 0) ||
        rewrite(min(x, c0) + c1 == 0, x == fold(-c1), c0 + c1 > 0) ||
        rewrite(max(x, c0) + c1 == 0, x <= c0, c0 + c1 == 0) ||
        rewrite(min(x, c0) + c1 == 0, c0 <= x, c0 + c1 == 0) ||
        rewrite(max(x, 0) == 0, x <= 0) ||
        rewrite(min(x, 0) == 0, 0 <= x)) {

        return mutate(std::move(rewrite.result), bounds);
    }

    if (rewrite(c0 == 0, fold(c0 == 0)) ||
        rewrite((x - y) + c0 == 0, x == y + fold(-c0)) ||
        rewrite(x + c0 == 0, x == fold(-c0)) ||
        rewrite(c0 - x == 0, x == c0)) {
        return rewrite.result;
    }

    if (const Sub *s = delta.as<Sub>()) {
        if (s->a.same_as(a) && s->b.same_as(b)) {
            return op;
        } else {
            Expr a = s->a;
            Expr b = s->b;
            if (no_overflow_int(op->a.type()) &&
                use_synthesized_rules &&
                (
#include "Simplify_EQ.inc"
                 )) {
                return mutate(std::move(rewrite.result), bounds);
            } else {
                return EQ::make(a, b);
            }
        }
    }

    return delta == make_zero(a.type());
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

}
}
