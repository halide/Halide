#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Add *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds && no_overflow_int(op->type)) {
        bounds->min_defined = a_bounds.min_defined &&
                              b_bounds.min_defined &&
                              add_with_overflow(64, a_bounds.min, b_bounds.min, &(bounds->min));
        bounds->max_defined = a_bounds.max_defined &&
                              b_bounds.max_defined &&
                              add_with_overflow(64, a_bounds.max, b_bounds.max, &(bounds->max));
        bounds->alignment = a_bounds.alignment + b_bounds.alignment;
        bounds->trim_bounds_using_alignment();
    }

    if (may_simplify(op->type)) {

        // Order commutative operations by node type
        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_bounds, b_bounds);
        }

        auto rewrite = IRMatcher::rewriter(IRMatcher::add(a, b), op->type);

        if (rewrite(IRMatcher::Overflow() + x, a) ||
            rewrite(x + IRMatcher::Overflow(), b) ||
            rewrite(x + 0, x) ||
            rewrite(0 + x, x)) {
            return rewrite.result;
        }

        // clang-format off
        if (EVAL_IN_LAMBDA
            (rewrite(c0 + c1, fold(c0 + c1)) ||
             rewrite(x + x, x * 2) ||
             rewrite(ramp(x, y, c0) + ramp(z, w, c0), ramp(x + z, y + w, c0)) ||
             rewrite(ramp(x, y, c0) + broadcast(z, c0), ramp(x + z, y, c0)) ||
             rewrite(broadcast(x, c0) + broadcast(y, c1), broadcast(x + broadcast(y, fold(c1/c0)), c0), c1 % c0 == 0) ||
             rewrite(broadcast(y, c1) + broadcast(x, c0), broadcast(x + broadcast(y, fold(c1/c0)), c0), c1 % c0 == 0) ||

             rewrite((x + broadcast(y, c0)) + broadcast(z, c1), x + broadcast(y + broadcast(z, fold(c1/c0)), c0), c1 % c0 == 0) ||
             rewrite((x + broadcast(z, c1)) + broadcast(y, c0), x + broadcast(y + broadcast(z, fold(c1/c0)), c0), c1 % c0 == 0) ||
             rewrite((broadcast(y, c0) + x) + broadcast(z, c1), x + broadcast(y + broadcast(z, fold(c1/c0)), c0), c1 % c0 == 0) ||
             rewrite((broadcast(z, c1) + x) + broadcast(y, c0), x + broadcast(y + broadcast(z, fold(c1/c0)), c0), c1 % c0 == 0) ||
             rewrite((x - broadcast(y, c0)) + broadcast(z, c1), x + broadcast(broadcast(z, fold(c1/c0)) - y, c0), c1 % c0 == 0) ||
             rewrite((x - broadcast(z, c1)) + broadcast(y, c0), x + broadcast(y - broadcast(z, fold(c1/c0)), c0), c1 % c0 == 0) ||
             rewrite((broadcast(y, c0) - x) + broadcast(z, c1), broadcast(y + broadcast(z, fold(c1/c0)), c0) - x, c1 % c0 == 0) ||
             rewrite((broadcast(z, c1) - x) + broadcast(y, c0), broadcast(y + broadcast(z, fold(c1/c0)), c0) - x, c1 % c0 == 0) ||
             rewrite(select(x, y, z) + select(x, w, u), select(x, y + w, z + u)) ||
             rewrite(select(x, c0, c1) + c2, select(x, fold(c0 + c2), fold(c1 + c2))) ||
             rewrite(select(x, y + c0, c1) + c2, select(x, y + fold(c0 + c2), fold(c1 + c2))) ||
             rewrite(select(x, c0, z + c1) + c2, select(x, fold(c0 + c2), z + fold(c1 + c2))) ||
             rewrite(select(x, y + c0, z + c1) + c2, select(x, y + fold(c0 + c2), z + fold(c1 + c2))) ||

             rewrite(ramp(broadcast(x, c0), y, c1) + broadcast(z, c2), ramp(broadcast(x + z, c0), y, c1), c2 == c0 * c1) ||
             rewrite(ramp(ramp(x, y, c0), z, c1) + broadcast(w, c2), ramp(ramp(x + w, y, c0), z, c1), c2 == c0 * c1) ||
             rewrite(select(x, y, z) + (select(x, u, v) + w), select(x, y + u, z + v) + w) ||
             rewrite(select(x, y, z) + (w + select(x, u, v)), select(x, y + u, z + v) + w) ||
             rewrite(select(x, y, z) + (select(x, u, v) - w), select(x, y + u, z + v) - w) ||
             rewrite(select(x, y, z) + (w - select(x, u, v)), select(x, y - u, z - v) + w) ||
             rewrite(select(x, c0 - y, c1) + c2, select(x, fold(c0 + c2) - y, fold(c1 + c2))) ||
             rewrite(select(x, y, z + c0) + c1, select(x, y + c1, z), (c0 + c1) == 0) ||
             rewrite(select(x, c0 - y, c1) + c2, fold(c0 + c2) - select(x, y, fold(c0 - c1))) ||

             rewrite(x + y*(-1), x - y) ||
             rewrite(x*(-1) + y, y - x) ||

             rewrite((x + c0) + c1, x + fold(c0 + c1)) ||
             rewrite((x + c0) + y, (x + y) + c0) ||
             rewrite(x + (y + c0), (x + y) + c0) ||
             rewrite((c0 - x) + c1, fold(c0 + c1) - x) ||
             rewrite((c0 - x) + y, (y - x) + c0) ||
             rewrite(max(x, y*c0 + z) + (u - y)*c0, max(x - y*c0, z) + u*c0) ||

             rewrite((x - y) + y, x) ||
             rewrite(x + (y - x), y) ||

             rewrite(((x - y) + z) + y, x + z) ||
             rewrite((z + (x - y)) + y, z + x) ||
             rewrite(x + ((y - x) + z), y + z) ||
             rewrite(x + (z + (y - x)), z + y) ||

             rewrite(x + (c0 - y), (x - y) + c0) ||
             rewrite((x - y) + (y - z), x - z) ||
             rewrite((x - y) + (z - x), z - y) ||

             rewrite((x - y) + (y + z), x + z) ||
             rewrite((x - y) + (z + y), x + z) ||

             rewrite(x + ((y - x) - z), y - z) ||
             rewrite(((x - y) - z) + y, x - z) ||

             rewrite(x + (y - (x + z)), y - z) ||
             rewrite(x + (y - (z + x)), y - z) ||
             rewrite((x - (y + z)) + y, x - z) ||
             rewrite((x - (y + z)) + z, x - y) ||

             rewrite(x + ((0 - y) - z), x - (y + z)) ||
             rewrite(((0 - x) - y) + z, z - (x + y)) ||
             rewrite(((c0 - x) - y) + c1, (fold(c0 + c1) - y) - x) ||

             rewrite(x*y + z*y, (x + z)*y) ||
             rewrite(x*y + y*z, (x + z)*y) ||
             rewrite(y*x + z*y, y*(x + z)) ||
             rewrite(y*x + y*z, y*(x + z)) ||
             rewrite(x*c0 + y*c1, (x + y*fold(c1/c0)) * c0, c1 % c0 == 0) ||
             rewrite(x*c0 + y*c1, (x*fold(c0/c1) + y) * c1, c0 % c1 == 0) ||

             // Hoist shuffles. The Shuffle visitor wants to sink
             // extract_elements to the leaves, and those count as degenerate
             // slices, so only hoist shuffles that grab more than one lane.
             rewrite(slice(x, c0, c1, c2) + slice(y, c0, c1, c2), slice(x + y, c0, c1, c2), c2 > 1 && lanes_of(x) == lanes_of(y)) ||
             rewrite(slice(x, c0, c1, c2) + (z + slice(y, c0, c1, c2)), slice(x + y, c0, c1, c2) + z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||
             rewrite(slice(x, c0, c1, c2) + (slice(y, c0, c1, c2) + z), slice(x + y, c0, c1, c2) + z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||
             rewrite(slice(x, c0, c1, c2) + (z - slice(y, c0, c1, c2)), slice(x - y, c0, c1, c2) + z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||
             rewrite(slice(x, c0, c1, c2) + (slice(y, c0, c1, c2) - z), slice(x + y, c0, c1, c2) - z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||

             (no_overflow(op->type) &&
              (rewrite(x + x*y, x * (y + 1)) ||
               rewrite(x + y*x, (y + 1) * x) ||
               rewrite(x*y + x, x * (y + 1)) ||
               rewrite(y*x + x, (y + 1) * x, !is_const(x)) ||
               rewrite((x + c0)/c1 + c2, (x + fold(c0 + c1*c2))/c1, c1 != 0) ||
               rewrite((x + (y + c0)/c1) + c2, x + (y + fold(c0 + c1*c2))/c1, c1 != 0) ||
               rewrite(((y + c0)/c1 + x) + c2, x + (y + fold(c0 + c1*c2))/c1, c1 != 0) ||
               rewrite((c0 - x)/c1 + c2, (fold(c0 + c1*c2) - x)/c1, c0 != 0 && c1 != 0) || // When c0 is zero, this would fight another rule
               rewrite(x + (x + y)/c0, (fold(c0 + 1)*x + y)/c0, c0 != 0) ||
               rewrite(x + (y + x)/c0, (fold(c0 + 1)*x + y)/c0, c0 != 0) ||
               rewrite(x + (y - x)/c0, (fold(c0 - 1)*x + y)/c0, c0 != 0) ||
               rewrite(x + (x - y)/c0, (fold(c0 + 1)*x - y)/c0, c0 != 0) ||
               rewrite((x - y)/c0 + x, (fold(c0 + 1)*x - y)/c0, c0 != 0) ||
               rewrite((y - x)/c0 + x, (y + fold(c0 - 1)*x)/c0, c0 != 0) ||
               rewrite((x + y)/c0 + x, (fold(c0 + 1)*x + y)/c0, c0 != 0) ||
               rewrite((y + x)/c0 + x, (y + fold(c0 + 1)*x)/c0, c0 != 0) ||
               rewrite(min(x, y - z) + z, min(x + z, y)) ||
               rewrite(min(y - z, x) + z, min(y, x + z)) ||
               rewrite(min(x, y + c0) + c1, min(x + c1, y), c0 + c1 == 0) ||
               rewrite(min(y + c0, x) + c1, min(y, x + c1), c0 + c1 == 0) ||
               rewrite(z + min(x, y - z), min(z + x, y)) ||
               rewrite(z + min(y - z, x), min(y, z + x)) ||
               rewrite(z + max(x, y - z), max(z + x, y)) ||
               rewrite(z + max(y - z, x), max(y, z + x)) ||
               rewrite(max(x, y - z) + z, max(x + z, y)) ||
               rewrite(max(y - z, x) + z, max(y, x + z)) ||
               rewrite(max(x, y + c0) + c1, max(x + c1, y), c0 + c1 == 0) ||
               rewrite(max(y + c0, x) + c1, max(y, x + c1), c0 + c1 == 0) ||
               rewrite(max(x, y) + min(x, y), x + y) ||
               rewrite(max(x, y) + min(y, x), x + y) ||

               rewrite(min(x, y + (z*c0)) + (z*c1), min(x + (z*c1), y), (c0 + c1) == 0) ||
               rewrite(min(x, (y*c0) + z) + (y*c1), min(x + (y*c1), z), (c0 + c1) == 0) ||
               rewrite(min(x, y*c0) + (y*c1), min(x + (y*c1), 0), (c0 + c1) == 0) ||
               rewrite(min(x + (y*c0), z) + (y*c1), min((y*c1) + z, x), (c0 + c1) == 0) ||
               rewrite(min((x*c0) + y, z) + (x*c1), min(y, (x*c1) + z), (c0 + c1) == 0) ||
               rewrite(min(x*c0, y) + (x*c1), min((x*c1) + y, 0), (c0 + c1) == 0) ||
               rewrite(max(x, y + (z*c0)) + (z*c1), max(x + (z*c1), y), (c0 + c1) == 0) ||
               rewrite(max(x, (y*c0) + z) + (y*c1), max(x + (y*c1), z), (c0 + c1) == 0) ||
               rewrite(max(x, y*c0) + (y*c1), max(x + (y*c1), 0), (c0 + c1) == 0) ||
               rewrite(max(x + (y*c0), z) + (y*c1), max(x, (y*c1) + z), (c0 + c1) == 0) ||
               rewrite(max((x*c0) + y, z) + (x*c1), max((x*c1) + z, y), (c0 + c1) == 0) ||
               rewrite(max(x*c0, y) + (x*c1), max((x*c1) + y, 0), (c0 + c1) == 0) ||

               false)) ||
             (no_overflow_int(op->type) &&
              (rewrite((x*(y/x)) + (y % x), select(x == 0, 0, y)) ||
               rewrite(((x/y)*y) + (x % y), select(y == 0, 0, x)) ||
               rewrite(w*(z + x/w) + x%w, select(w == 0, 0, z*w + x)) ||
               rewrite((z + x/w)*w + x%w, select(w == 0, 0, z*w + x)) ||
               rewrite(w*(x/w + z) + x%w, select(w == 0, 0, x + z*w)) ||
               rewrite((x/w + z)*w + x%w, select(w == 0, 0, x + z*w)) ||
               rewrite(x%w + (w*(x/w) + z), select(w == 0, 0, x) + z) ||
               rewrite(x%w + ((x/w)*w + z), select(w == 0, 0, x) + z) ||
               rewrite(x%w + (w*(x/w) - z), select(w == 0, 0, x) - z) ||
               rewrite(x%w + ((x/w)*w - z), select(w == 0, 0, x) - z) ||
               rewrite(x%w + (z + w*(x/w)), select(w == 0, 0, x) + z) ||
               rewrite(x%w + (z + (x/w)*w), select(w == 0, 0, x) + z) ||
               rewrite(w*(x/w) + (x%w + z), select(w == 0, 0, x) + z) ||
               rewrite((x/w)*w + (x%w + z), select(w == 0, 0, x) + z) ||
               rewrite(w*(x/w) + (x%w - z), select(w == 0, 0, x) - z) ||
               rewrite((x/w)*w + (x%w - z), select(w == 0, 0, x) - z) ||
               rewrite(w*(x/w) + (z + x%w), select(w == 0, 0, x) + z) ||
               rewrite((x/w)*w + (z + x%w), select(w == 0, 0, x) + z) ||
               rewrite(x/2 + x%2, (x + 1) / 2) ||

               rewrite(x + ((c0 - x)/c1)*c1, c0 - ((c0 - x) % c1), c1 > 0) ||
               rewrite(x + ((c0 - x)/c1 + y)*c1, y * c1 - ((c0 - x) % c1) + c0, c1 > 0) ||
               rewrite(x + (y + (c0 - x)/c1)*c1, y * c1 - ((c0 - x) % c1) + c0, c1 > 0) ||

               false)))) {
            return mutate(rewrite.result, bounds);
        }
        // clang-format on
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Add::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
