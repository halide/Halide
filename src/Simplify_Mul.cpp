#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

namespace {
int64_t saturating_mul(int64_t a, int64_t b) {
    if (a == 0 || b == 0) {
        return 0;
    } else if (a == INT64_MIN && b > 0) {
        return INT64_MIN;
    } else if (a == INT64_MIN && b < 0) {
        return INT64_MAX;
    } else if (a == INT64_MAX && b > 0) {
        return INT64_MAX;
    } else if (a == INT64_MAX && b < 0) {
        return INT64_MIN;
    } else if (b == INT64_MIN && a > 0) {
        return INT64_MIN;
    } else if (b == INT64_MIN && a < 0) {
        return INT64_MAX;
    } else if (b == INT64_MAX && a > 0) {
        return INT64_MAX;
    } else if (b == INT64_MAX && a < 0) {
        return INT64_MIN;
    } else if (mul_would_overflow(64, a, b)) {
        if ((a > 0) == (b > 0)) {
            return INT64_MAX;
        } else {
            return INT64_MIN;
        }
    } else {
        return a * b;
    }
}
}  // namespace

Expr Simplify::visit(const Mul *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds && no_overflow_int(op->type)) {
        // Just use INT64_MAX/MIN to represent infinity, and take the four extrema.
        int64_t a_max = a_bounds.max_defined ? a_bounds.max : INT64_MAX;
        int64_t b_max = b_bounds.max_defined ? b_bounds.max : INT64_MAX;
        int64_t a_min = a_bounds.min_defined ? a_bounds.min : INT64_MIN;
        int64_t b_min = b_bounds.min_defined ? b_bounds.min : INT64_MIN;

        bounds->min_defined = bounds->max_defined = true;
        int64_t v1 = saturating_mul(a_min, b_min);
        int64_t v2 = saturating_mul(a_min, b_max);
        int64_t v3 = saturating_mul(a_max, b_min);
        int64_t v4 = saturating_mul(a_max, b_max);
        bounds->min = std::min(std::min(v1, v2), std::min(v3, v4));
        bounds->max = std::max(std::max(v1, v2), std::max(v3, v4));

        if (bounds->max == INT64_MAX) {
            bounds->max_defined = false;
            bounds->max = 0;
        }
        if (bounds->min == INT64_MIN) {
            bounds->min_defined = false;
            bounds->min = 0;
        }

        // sanity check
        /*
        bool a_could_be_negative = !(a_bounds.min_defined && a_bounds.min >= 0);
        bool a_could_be_positive = !(a_bounds.max_defined && a_bounds.max <= 0);
        bool b_could_be_negative = !(b_bounds.min_defined && b_bounds.min >= 0);
        bool b_could_be_positive = !(b_bounds.max_defined && b_bounds.max <= 0);
        bool could_be_pos_inf = ((!a_bounds.max_defined && b_could_be_positive) ||
                                 (!b_bounds.max_defined && a_could_be_positive) ||
                                 (!a_bounds.min_defined && b_could_be_negative) ||
                                 (!b_bounds.min_defined && a_could_be_negative));
        bool could_be_neg_inf = ((!a_bounds.max_defined && b_could_be_negative) ||
                                 (!b_bounds.max_defined && a_could_be_negative) ||
                                 (!a_bounds.min_defined && b_could_be_positive) ||
                                 (!b_bounds.min_defined && a_could_be_positive));

        internal_assert(bounds->max_defined == !could_be_pos_inf)
            << a_bounds.min_defined << " " << a_bounds.min << " "
            << a_bounds.max_defined << " " << a_bounds.max << "\n"
            << b_bounds.min_defined << " " << b_bounds.min << " "
            << b_bounds.max_defined << " " << b_bounds.max << "\n"
            << bounds->min_defined << " " << bounds->min << " "
            << bounds->max_defined << " " << bounds->max << "\n";
        internal_assert(bounds->min_defined == !could_be_neg_inf);
        */

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
            #ifdef EXCLUDE_INVALID_ORDERING_RULES
            rewrite((x - y) * c0, (y - x) * fold(-c0), c0 < 0 && -c0 > 0) ||
            #endif
            rewrite((x * c0) * c1, x * fold(c0 * c1), !overflows(c0 * c1)) ||
            rewrite((x * c0) * y, (x * y) * c0, !is_const(y)) ||
            rewrite(x * (y * c0), (x * y) * c0) ||
            rewrite(x * -1, 0 - x) ||
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

        if (no_overflow_int(op->type) &&
            use_synthesized_rules &&
            (
#include "Simplify_Mul.inc"
             )) {
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
