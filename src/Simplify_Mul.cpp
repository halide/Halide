#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

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
            int64_t v1 = a_bounds.min * b_bounds.min;
            int64_t v2 = a_bounds.min * b_bounds.max;
            int64_t v3 = a_bounds.max * b_bounds.min;
            int64_t v4 = a_bounds.max * b_bounds.max;
            bounds->min = std::min(std::min(v1, v2), std::min(v3, v4));
            bounds->max = std::max(std::max(v1, v2), std::max(v3, v4));
        } else if ((a_bounds.max_defined && b_bounded && b_positive) ||
                   (b_bounds.max_defined && a_bounded && a_positive)) {
            bounds->max_defined = true;
            bounds->max = a_bounds.max * b_bounds.max;
        } else if ((a_bounds.min_defined && b_bounded && b_positive) ||
                   (b_bounds.min_defined && a_bounded && a_positive)) {
            bounds->min_defined = true;
            bounds->min = a_bounds.min * b_bounds.min;
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
            rewrite(ramp(x, y) * broadcast(z), ramp(x * z, y * z, op->type.lanes())) ||

            // Added during the synthesis work to clean up some of the
            // interpreter expressions we generate for masking
            // operations.
            rewrite(select(x, 1, 0) * y, select(x, y, 0)) ||
            rewrite(select(x, 0, 1) * y, select(x, 0, y)) ||

            false) {
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
