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

        bounds->min = INT64_MAX;
        bounds->max = INT64_MIN;

        const bool a_positive = a_bounds.min_defined && a_bounds.min >= 0;
        const bool a_negative = a_bounds.max_defined && a_bounds.max <= 0;
        const bool b_positive = b_bounds.min_defined && b_bounds.min >= 0;
        const bool b_negative = b_bounds.max_defined && b_bounds.max <= 0;

        const int bits = std::min(64, op->type.bits());
        const bool min_min_exists = a_bounds.min_defined && b_bounds.min_defined && !mul_would_overflow(bits, a_bounds.min, b_bounds.min);
        const bool max_min_exists = a_bounds.max_defined && b_bounds.min_defined && !mul_would_overflow(bits, a_bounds.max, b_bounds.min);
        const bool min_max_exists = a_bounds.min_defined && b_bounds.max_defined && !mul_would_overflow(bits, a_bounds.min, b_bounds.max);
        const bool max_max_exists = a_bounds.max_defined && b_bounds.max_defined && !mul_would_overflow(bits, a_bounds.max, b_bounds.max);
        const bool all_products_exist = min_min_exists && max_min_exists && max_max_exists && min_max_exists;

        bounds->min_defined = (all_products_exist ||
                               (a_positive && max_min_exists) ||
                               (b_positive && min_max_exists) ||
                               (a_negative && min_max_exists) ||
                               (b_negative && max_min_exists));
        bounds->max_defined = (all_products_exist ||
                               (a_positive && max_max_exists) ||
                               (b_positive && max_max_exists) ||
                               (b_negative && min_min_exists) ||
                               (a_negative && min_min_exists));

        // Enumerate all possible values for the min and max and take the extreme values.
        if (min_min_exists) {
            int64_t v = a_bounds.min * b_bounds.min;
            bounds->min = std::min(bounds->min, v);
            bounds->max = std::max(bounds->max, v);
        }

        if (min_max_exists) {
            int64_t v = a_bounds.min * b_bounds.max;
            bounds->min = std::min(bounds->min, v);
            bounds->max = std::max(bounds->max, v);
        }

        if (max_min_exists) {
            int64_t v = a_bounds.max * b_bounds.min;
            bounds->min = std::min(bounds->min, v);
            bounds->max = std::max(bounds->max, v);
        }

        if (max_max_exists) {
            int64_t v = a_bounds.max * b_bounds.max;
            bounds->min = std::min(bounds->min, v);
            bounds->max = std::max(bounds->max, v);
        }

        if (!bounds->min_defined) {
            bounds->min = 0;
        }
        if (!bounds->max_defined) {
            bounds->max = 0;
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
