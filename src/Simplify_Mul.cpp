#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

namespace {
int64_t saturating_mul(int64_t a, int64_t b) {
    if (mul_would_overflow(64, a, b)) {
        if ((a > 0) == (b > 0)) {
            return INT64_MAX;
        } else {
            return INT64_MIN;
        }
    } else {
        return a * b;
    }
}
}

Expr Simplify::visit(const Mul *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds && no_overflow_int(op->type)) {
        bool a_positive = a_bounds.min_defined && a_bounds.min > 0;
        bool b_positive = b_bounds.min_defined && b_bounds.min > 0;
        bool a_bounded = a_bounds.min_defined && a_bounds.max_defined;
        bool b_bounded = b_bounds.min_defined && b_bounds.max_defined;

        if (a_bounded && b_bounded) {
            bounds->min_defined = bounds->max_defined = true;
            int64_t v1 = saturating_mul(a_bounds.min, b_bounds.min);
            int64_t v2 = saturating_mul(a_bounds.min, b_bounds.max);
            int64_t v3 = saturating_mul(a_bounds.max, b_bounds.min);
            int64_t v4 = saturating_mul(a_bounds.max, b_bounds.max);
            bounds->min = std::min(std::min(v1, v2), std::min(v3, v4));
            bounds->max = std::max(std::max(v1, v2), std::max(v3, v4));
        } else if ((a_bounds.max_defined && b_bounded && b_positive) ||
                   (b_bounds.max_defined && a_bounded && a_positive)) {
            bounds->max_defined = true;
            bounds->max = saturating_mul(a_bounds.max, b_bounds.max);
        } else if ((a_bounds.min_defined && b_bounded && b_positive) ||
                   (b_bounds.min_defined && a_bounded && a_positive)) {
            bounds->min_defined = true;
            bounds->min = saturating_mul(a_bounds.min, b_bounds.min);
        }

        if (bounds->max_defined && bounds->max == INT64_MAX) {
            // Assume it saturated to avoid overflow. This gives up a
            // single representable value at the top end of the range
            // to represent infinity.
            bounds->max_defined = false;
            bounds->max = 0;
        }
        if (bounds->min_defined && bounds->min == INT64_MIN) {
            bounds->min_defined = false;
            bounds->min = 0;
        }

        bounds->alignment = a_bounds.alignment * b_bounds.alignment;
        bounds->trim_bounds_using_alignment();
    }

    if (may_simplify(op->type)) {

        // Order commutative operations by node type
        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_bounds, b_bounds);
        }

        auto rewrite = IRMatcher::rewriter(IRMatcher::mul(a, b), op->type);
        if (rewrite(c0 * c1, fold(c0 * c1)) ||
            rewrite(IRMatcher::Indeterminate() * x, a) ||
            rewrite(x * IRMatcher::Indeterminate(), b) ||
            rewrite(IRMatcher::Overflow() * x, a) ||
            rewrite(x * IRMatcher::Overflow(), b) ||
            rewrite(0 * x, 0) ||
            rewrite(1 * x, x) ||
            rewrite(x * 0, 0) ||
            rewrite(x * 1, x)) {
            return rewrite.result;
        }

        if (rewrite((x + c0) * c1, x * c1 + fold(c0 * c1), !overflows(c0 * c1)) ||
            rewrite((x - y) * c0, (y - x) * fold(-c0), c0 < 0 && -c0 > 0) ||
            rewrite((x * c0) * c1, x * fold(c0 * c1), !overflows(c0 * c1)) ||
            rewrite((x * c0) * y, (x * y) * c0, !is_const(y)) ||
            rewrite(x * (y * c0), (x * y) * c0) ||
            rewrite(max(x, y) * min(x, y), x * y) ||
            rewrite(max(x, y) * min(y, x), y * x) ||
            rewrite(broadcast(x) * broadcast(y), broadcast(x * y, op->type.lanes())) ||
            rewrite(ramp(x, y) * broadcast(z), ramp(x * z, y * z, op->type.lanes()))) {
            return mutate(std::move(rewrite.result), bounds);
        }
    }

    const Shuffle *shuffle_a = a.as<Shuffle>();
    const Shuffle *shuffle_b = b.as<Shuffle>();
    if (shuffle_a && shuffle_b &&
        shuffle_a->is_slice() &&
        shuffle_b->is_slice()) {
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return hoist_slice_vector<Mul>(op);
        } else {
            return hoist_slice_vector<Mul>(Mul::make(a, b));
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Mul::make(a, b);
    }
}

}
}
