#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const LT *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    const int lanes = op->type.lanes();
    Type ty = a.type();

    if (truths.count(op)) {
        return const_true(lanes);
    } else if (falsehoods.count(op)) {
        return const_false(lanes);
    }

    if (may_simplify(ty)) {

        // Prove or disprove using bounds analysis
        if (a_bounds.max_defined && b_bounds.min_defined && a_bounds.max < b_bounds.min) {
            return const_true(lanes);
        }

        if (a_bounds.min_defined && b_bounds.max_defined && a_bounds.min >= b_bounds.max) {
            return const_false(lanes);
        }

        auto rewrite = IRMatcher::rewriter(IRMatcher::lt(a, b), op->type, ty);

        if (EVAL_IN_LAMBDA
            (rewrite(c0 < c1, fold(c0 < c1)) ||
             rewrite(x < x, false) ||
             rewrite(x < ty.min(), false) ||
             rewrite(ty.max() < x, false) ||

             rewrite(max(x, y) < x, false) ||
             rewrite(max(y, x) < x, false) ||
             rewrite(x < min(x, y), false) ||
             rewrite(x < min(y, x), false) ||

             // Comparisons of ramps and broadcasts. If the first
             // and last lanes are provably < or >= the broadcast
             // we can collapse the comparison.
             (no_overflow(op->type) &&
              (rewrite(ramp(x, c1) < broadcast(z), true, can_prove(x + fold(max(0, c1 * (lanes - 1))) < z, this)) ||
               rewrite(ramp(x, c1) < broadcast(z), false, can_prove(x + fold(min(0, c1 * (lanes - 1))) >= z, this)) ||
               rewrite(broadcast(z) < ramp(x, c1), true, can_prove(z < x + fold(min(0, c1 * (lanes - 1))), this)) ||
               rewrite(broadcast(z) < ramp(x, c1), false, can_prove(z >= x + fold(max(0, c1 * (lanes - 1))), this)))))) {
            return rewrite.result;
        }

        if (rewrite(broadcast(x) < broadcast(y), broadcast(x < y, lanes)) ||
            (no_overflow(ty) && EVAL_IN_LAMBDA
             (rewrite(ramp(x, y) < ramp(z, y), broadcast(x < z, lanes)) ||
              // Merge RHS constant additions with a constant LHS
              rewrite(x + c0 < c1, x < fold(c1 - c0)) ||
              rewrite(c0 < x + c1, fold(c0 - c1) < x) ||

              // Move constants to the RHS
              rewrite(x + c0 < y, x < y + fold(-c0)) ||

              // Normalize subtractions to additions to cut down on cases to consider
              rewrite(x - y < z, x < z + y) ||
              rewrite(z < x - y, z + y < x) ||

              rewrite((x - y) + z < w, x + z < y + w) ||
              rewrite(z + (x - y) < w, x + z < y + w) ||
              rewrite(w < (x - y) + z, w + y < x + z) ||
              rewrite(w < z + (x - y), w + y < x + z) ||

              rewrite(((x - y) + z) + u < w, x + z + u < w + y) ||
              rewrite((z + (x - y)) + u < w, x + z + u < w + y) ||
              rewrite(u + ((x - y) + z) < w, x + z + u < w + y) ||
              rewrite(u + (z + (x - y)) < w, x + z + u < w + y) ||

              rewrite(w < ((x - y) + z) + u, w + y < x + z + u) ||
              rewrite(w < (z + (x - y)) + u, w + y < x + z + u) ||
              rewrite(w < u + ((x - y) + z), w + y < x + z + u) ||
              rewrite(w < u + (z + (x - y)), w + y < x + z + u) ||

              // Cancellations in linear expressions
              // 1 < 2
              rewrite(x < x + y, 0 < y) ||
              rewrite(x < y + x, 0 < y) ||

              // 2 < 1
              rewrite(x + y < x, y < 0) ||
              rewrite(y + x < x, y < 0) ||

              // 2 < 2
              rewrite(x + y < x + z, y < z) ||
              rewrite(x + y < z + x, y < z) ||
              rewrite(y + x < x + z, y < z) ||
              rewrite(y + x < z + x, y < z) ||

              // 3 < 2
              rewrite((x + y) + w < x + z, y + w < z) ||
              rewrite((y + x) + w < x + z, y + w < z) ||
              rewrite(w + (x + y) < x + z, y + w < z) ||
              rewrite(w + (y + x) < x + z, y + w < z) ||
              rewrite((x + y) + w < z + x, y + w < z) ||
              rewrite((y + x) + w < z + x, y + w < z) ||
              rewrite(w + (x + y) < z + x, y + w < z) ||
              rewrite(w + (y + x) < z + x, y + w < z) ||

              // 2 < 3
              rewrite(x + z < (x + y) + w, z < y + w) ||
              rewrite(x + z < (y + x) + w, z < y + w) ||
              rewrite(x + z < w + (x + y), z < y + w) ||
              rewrite(x + z < w + (y + x), z < y + w) ||
              rewrite(z + x < (x + y) + w, z < y + w) ||
              rewrite(z + x < (y + x) + w, z < y + w) ||
              rewrite(z + x < w + (x + y), z < y + w) ||
              rewrite(z + x < w + (y + x), z < y + w) ||

              // 3 < 3
              rewrite((x + y) + w < (x + z) + u, y + w < z + u) ||
              rewrite((y + x) + w < (x + z) + u, y + w < z + u) ||
              rewrite((x + y) + w < (z + x) + u, y + w < z + u) ||
              rewrite((y + x) + w < (z + x) + u, y + w < z + u) ||
              rewrite(w + (x + y) < (x + z) + u, y + w < z + u) ||
              rewrite(w + (y + x) < (x + z) + u, y + w < z + u) ||
              rewrite(w + (x + y) < (z + x) + u, y + w < z + u) ||
              rewrite(w + (y + x) < (z + x) + u, y + w < z + u) ||
              rewrite((x + y) + w < u + (x + z), y + w < z + u) ||
              rewrite((y + x) + w < u + (x + z), y + w < z + u) ||
              rewrite((x + y) + w < u + (z + x), y + w < z + u) ||
              rewrite((y + x) + w < u + (z + x), y + w < z + u) ||
              rewrite(w + (x + y) < u + (x + z), y + w < z + u) ||
              rewrite(w + (y + x) < u + (x + z), y + w < z + u) ||
              rewrite(w + (x + y) < u + (z + x), y + w < z + u) ||
              rewrite(w + (y + x) < u + (z + x), y + w < z + u) ||

              // Cancel a multiplication
              rewrite(x * c0 < y * c0, x < y, c0 > 0) ||
              rewrite(x * c0 < y * c0, y < x, c0 < 0) ||

              (ty.is_int()   && rewrite(x * c0 < c1, x < fold((c1 + c0 - 1) / c0), c0 > 0)) ||
              (ty.is_float() && rewrite(x * c0 < c1, x < fold(c1 / c0), c0 > 0)) ||
              rewrite(c1 < x * c0, fold(c1 / c0) < x, c0 > 0) ||

              // Multiply-out a division
              rewrite(x / c0 < c1, x < c1 * c0, c0 > 0) ||
              (ty.is_int() && rewrite(c0 < x / c1, fold((c0 + 1) * c1 - 1) < x, c1 > 0)) ||
              (ty.is_float() && rewrite(c0 < x / c1, fold(c0 * c1) < x, c1 > 0)) ||

              // We want to break max(x, y) < z into x < z && y <
              // z in cases where one of those two terms is going
              // to fold.
              rewrite(min(x + c0, y) < x + c1, fold(c0 < c1) || y < x + c1) ||
              rewrite(min(y, x + c0) < x + c1, fold(c0 < c1) || y < x + c1) ||
              rewrite(max(x + c0, y) < x + c1, fold(c0 < c1) && y < x + c1) ||
              rewrite(max(y, x + c0) < x + c1, fold(c0 < c1) && y < x + c1) ||

              rewrite(x < min(x + c0, y) + c1, fold(0 < c0 + c1) && x < y + c1) ||
              rewrite(x < min(y, x + c0) + c1, fold(0 < c0 + c1) && x < y + c1) ||
              rewrite(x < max(x + c0, y) + c1, fold(0 < c0 + c1) || x < y + c1) ||
              rewrite(x < max(y, x + c0) + c1, fold(0 < c0 + c1) || x < y + c1) ||

              // Special cases where c0 == 0
              rewrite(min(x, y) < x + c1, fold(0 < c1) || y < x + c1) ||
              rewrite(min(y, x) < x + c1, fold(0 < c1) || y < x + c1) ||
              rewrite(max(x, y) < x + c1, fold(0 < c1) && y < x + c1) ||
              rewrite(max(y, x) < x + c1, fold(0 < c1) && y < x + c1) ||

              rewrite(x < min(x, y) + c1, fold(0 < c1) && x < y + c1) ||
              rewrite(x < min(y, x) + c1, fold(0 < c1) && x < y + c1) ||
              rewrite(x < max(x, y) + c1, fold(0 < c1) || x < y + c1) ||
              rewrite(x < max(y, x) + c1, fold(0 < c1) || x < y + c1) ||

              // Special cases where c1 == 0
              rewrite(min(x + c0, y) < x, fold(c0 < 0) || y < x) ||
              rewrite(min(y, x + c0) < x, fold(c0 < 0) || y < x) ||
              rewrite(max(x + c0, y) < x, fold(c0 < 0) && y < x) ||
              rewrite(max(y, x + c0) < x, fold(c0 < 0) && y < x) ||

              rewrite(x < min(x + c0, y), fold(0 < c0) && x < y) ||
              rewrite(x < min(y, x + c0), fold(0 < c0) && x < y) ||
              rewrite(x < max(x + c0, y), fold(0 < c0) || x < y) ||
              rewrite(x < max(y, x + c0), fold(0 < c0) || x < y) ||

              // Special cases where c0 == c1 == 0
              rewrite(min(x, y) < x, y < x) ||
              rewrite(min(y, x) < x, y < x) ||
              rewrite(x < max(x, y), x < y) ||
              rewrite(x < max(y, x), x < y) ||

              // Special case where x is constant
              rewrite(min(y, c0) < c1, fold(c0 < c1) || y < c1) ||
              rewrite(max(y, c0) < c1, fold(c0 < c1) && y < c1) ||
              rewrite(c1 < min(y, c0), fold(c1 < c0) && c1 < y) ||
              rewrite(c1 < max(y, c0), fold(c1 < c0) || c1 < y) ||

              // Comparisons with selects:
              // x < select(c, t, f) == c && (x < t) || !c && (x < f)
              // This is profitable when x < t or x < f is statically provable
              rewrite(x < select(y, x + c0, z), !y && (x < z), c0 <= 0) ||
              rewrite(x < select(y, x + c0, z), y || (x < z), c0 > 0) ||
              rewrite(x < select(y, z, x + c0), y && (x < z), c0 <= 0) ||
              rewrite(x < select(y, z, x + c0), !y || (x < z), c0 > 0) ||

              rewrite(x < select(y, x + c0, z) + c1, !y && (x < z + c1), c0 + c1 <= 0) ||
              rewrite(x < select(y, x + c0, z) + c1, y || (x < z + c1), c0 + c1 > 0) ||
              rewrite(x < select(y, z, x + c0) + c1, y && (x < z + c1), c0 + c1 <= 0) ||
              rewrite(x < select(y, z, x + c0) + c1, !y || (x < z + c1), c0 + c1 > 0) ||

              rewrite(select(y, x + c0, z) < x, !y && (z < x), c0 >= 0) ||
              rewrite(select(y, x + c0, z) < x, y || (z < x), c0 < 0) ||
              rewrite(select(y, z, x + c0) < x, y && (z < x), c0 >= 0) ||
              rewrite(select(y, z, x + c0) < x, !y || (z < x), c0 < 0) ||

              rewrite(select(y, x + c0, z) < x + c1, !y && (z < x + c1), c0 >= c1) ||
              rewrite(select(y, x + c0, z) < x + c1, y || (z < x + c1), c0 < c1) ||
              rewrite(select(y, z, x + c0) < x + c1, y && (z < x + c1), c0 >= c1) ||
              rewrite(select(y, z, x + c0) < x + c1, !y || (z < x + c1), c0 < c1) ||

              // Normalize comparison of ramps to a comparison of a ramp and a broadacst
              rewrite(ramp(x, y) < ramp(z, w), ramp(x - z, y - w, lanes) < 0))) ||

            (no_overflow_int(ty) && EVAL_IN_LAMBDA
             (rewrite(x * c0 < y * c1, x < y * fold(c1 / c0), c1 % c0 == 0 && c0 > 0) ||
              rewrite(x * c0 < y * c1, x * fold(c0 / c1) < y, c0 % c1 == 0 && c1 > 0) ||

              rewrite(x * c0 < y * c0 + c1, x < y + fold((c1 + c0 - 1)/c0), c0 > 0) ||
              rewrite(x * c0 + c1 < y * c0, x + fold(c1/c0) < y, c0 > 0) ||

              // Comparison of stair-step functions. The basic transformation is:
              //   ((x + y)/c1)*c1 < x
              // = (x + y) - (x + y) % c1 < x (when c1 > 0)
              // = y - (x + y) % c1 < 0
              // = y < (x + y) % c1
              // This cancels x but duplicates y, so we only do it when y is a constant.

              // A more general version with extra terms w and z
              rewrite(((x + c0)/c1)*c1 + w < x + z, (w + c0) < ((x + c0) % c1) + z, c1 > 0) ||
              rewrite(w + ((x + c0)/c1)*c1 < x + z, (w + c0) < ((x + c0) % c1) + z, c1 > 0) ||
              rewrite(((x + c0)/c1)*c1 + w < z + x, (w + c0) < ((x + c0) % c1) + z, c1 > 0) ||
              rewrite(w + ((x + c0)/c1)*c1 < z + x, (w + c0) < ((x + c0) % c1) + z, c1 > 0) ||
              rewrite(x + z < ((x + c0)/c1)*c1 + w, ((x + c0) % c1) + z < w + c0, c1 > 0) ||
              rewrite(x + z < w + ((x + c0)/c1)*c1, ((x + c0) % c1) + z < w + c0, c1 > 0) ||
              rewrite(z + x < ((x + c0)/c1)*c1 + w, ((x + c0) % c1) + z < w + c0, c1 > 0) ||
              rewrite(z + x < w + ((x + c0)/c1)*c1, ((x + c0) % c1) + z < w + c0, c1 > 0) ||

              // w = 0
              rewrite(((x + c0)/c1)*c1 < x + z, c0 < ((x + c0) % c1) + z, c1 > 0) ||
              rewrite(((x + c0)/c1)*c1 < z + x, c0 < ((x + c0) % c1) + z, c1 > 0) ||
              rewrite(x + z < ((x + c0)/c1)*c1, ((x + c0) % c1) + z < c0, c1 > 0) ||
              rewrite(z + x < ((x + c0)/c1)*c1, ((x + c0) % c1) + z < c0, c1 > 0) ||

              // z = 0
              rewrite(((x + c0)/c1)*c1 + w < x, (w + c0) < ((x + c0) % c1), c1 > 0) ||
              rewrite(w + ((x + c0)/c1)*c1 < x, (w + c0) < ((x + c0) % c1), c1 > 0) ||
              rewrite(x < ((x + c0)/c1)*c1 + w, ((x + c0) % c1) < w + c0, c1 > 0) ||
              rewrite(x < w + ((x + c0)/c1)*c1, ((x + c0) % c1) < w + c0, c1 > 0) ||

              // c0 = 0
              rewrite((x/c1)*c1 + w < x + z, w < (x % c1) + z, c1 > 0) ||
              rewrite(w + (x/c1)*c1 < x + z, w < (x % c1) + z, c1 > 0) ||
              rewrite((x/c1)*c1 + w < z + x, w < (x % c1) + z, c1 > 0) ||
              rewrite(w + (x/c1)*c1 < z + x, w < (x % c1) + z, c1 > 0) ||
              rewrite(x + z < (x/c1)*c1 + w, (x % c1) + z < w, c1 > 0) ||
              rewrite(x + z < w + (x/c1)*c1, (x % c1) + z < w, c1 > 0) ||
              rewrite(z + x < (x/c1)*c1 + w, (x % c1) + z < w, c1 > 0) ||
              rewrite(z + x < w + (x/c1)*c1, (x % c1) + z < w, c1 > 0) ||

              // w = 0, z = 0
              rewrite(((x + c0)/c1)*c1 < x, c0 < ((x + c0) % c1), c1 > 0) ||
              rewrite(x < ((x + c0)/c1)*c1, ((x + c0) % c1) < c0, c1 > 0) ||

              // w = 0, c0 = 0
              rewrite((x/c1)*c1 < x + z, 0 < (x % c1) + z, c1 > 0) ||
              rewrite((x/c1)*c1 < z + x, 0 < (x % c1) + z, c1 > 0) ||
              rewrite(x + z < (x/c1)*c1, (x % c1) + z < 0, c1 > 0) ||
              rewrite(z + x < (x/c1)*c1, (x % c1) + z < 0, c1 > 0) ||

              // z = 0, c0 = 0
              rewrite((x/c1)*c1 + w < x, w < (x % c1), c1 > 0) ||
              rewrite(w + (x/c1)*c1 < x, w < (x % c1), c1 > 0) ||
              rewrite(x < (x/c1)*c1 + w, (x % c1) < w, c1 > 0) ||
              rewrite(x < w + (x/c1)*c1, (x % c1) < w, c1 > 0) ||

              // z = 0, c0 = 0, w = 0
              rewrite((x/c1)*c1 < x, (x % c1) != 0, c1 > 0) ||
              rewrite(x < (x/c1)*c1, false, c1 > 0) ||

              // Cancel a division
              rewrite((x + c1)/c0 < (x + c2)/c0, false, c0 > 0 && c1 >= c2) ||
              rewrite((x + c1)/c0 < (x + c2)/c0, true, c0 > 0 && c1 <= c2 - c0) ||
              // c1 == 0
              rewrite(x/c0 < (x + c2)/c0, false, c0 > 0 && 0 >= c2) ||
              rewrite(x/c0 < (x + c2)/c0, true, c0 > 0 && 0 <= c2 - c0) ||
              // c2 == 0
              rewrite((x + c1)/c0 < x/c0, false, c0 > 0 && c1 >= 0) ||
              rewrite((x + c1)/c0 < x/c0, true, c0 > 0 && c1 <= 0 - c0) ||

              // The addition on the right could be outside
              rewrite((x + c1)/c0 < x/c0 + c2, false, c0 > 0 && c1 >= c2 * c0) ||
              rewrite((x + c1)/c0 < x/c0 + c2, true, c0 > 0 && c1 <= c2 * c0 - c0) ||

              // With a confounding max or min
              rewrite((x + c1)/c0 < (min(x/c0, y) + c2), false, c0 > 0 && c1 >= c2 * c0) ||
              rewrite((x + c1)/c0 < (max(x/c0, y) + c2), true, c0 > 0 && c1 <= c2 * c0 - c0) ||
              rewrite((x + c1)/c0 < min((x + c2)/c0, y), false, c0 > 0 && c1 >= c2) ||
              rewrite((x + c1)/c0 < max((x + c2)/c0, y), true, c0 > 0 && c1 <= c2 - c0) ||
              rewrite((x + c1)/c0 < min(x/c0, y), false, c0 > 0 && c1 >= 0) ||
              rewrite((x + c1)/c0 < max(x/c0, y), true, c0 > 0 && c1 <= 0 - c0) ||

              rewrite((x + c1)/c0 < (min(y, x/c0) + c2), false, c0 > 0 && c1 >= c2 * c0) ||
              rewrite((x + c1)/c0 < (max(y, x/c0) + c2), true, c0 > 0 && c1 <= c2 * c0 - c0) ||
              rewrite((x + c1)/c0 < min(y, (x + c2)/c0), false, c0 > 0 && c1 >= c2) ||
              rewrite((x + c1)/c0 < max(y, (x + c2)/c0), true, c0 > 0 && c1 <= c2 - c0) ||
              rewrite((x + c1)/c0 < min(y, x/c0), false, c0 > 0 && c1 >= 0) ||
              rewrite((x + c1)/c0 < max(y, x/c0), true, c0 > 0 && c1 <= 0 - c0) ||

              rewrite(max((x + c2)/c0, y) < (x + c1)/c0, false, c0 > 0 && c2 >= c1) ||
              rewrite(min((x + c2)/c0, y) < (x + c1)/c0, true, c0 > 0 && c2 <= c1 - c0) ||
              rewrite(max(x/c0, y) < (x + c1)/c0, false, c0 > 0 && 0 >= c1) ||
              rewrite(min(x/c0, y) < (x + c1)/c0, true, c0 > 0 && 0 <= c1 - c0) ||
              rewrite(max(y, (x + c2)/c0) < (x + c1)/c0, false, c0 > 0 && c2 >= c1) ||
              rewrite(min(y, (x + c2)/c0) < (x + c1)/c0, true, c0 > 0 && c2 <= c1 - c0) ||
              rewrite(max(y, x/c0) < (x + c1)/c0, false, c0 > 0 && 0 >= c1) ||
              rewrite(min(y, x/c0) < (x + c1)/c0, true, c0 > 0 && 0 <= c1 - c0) ||

              // Same as above with c1 outside the division, with redundant cases removed.
              rewrite(max((x + c2)/c0, y) < x/c0 + c1, false, c0 > 0 && c2 >= c1 * c0) ||
              rewrite(min((x + c2)/c0, y) < x/c0 + c1, true, c0 > 0 && c2 <= c1 * c0 - c0) ||
              rewrite(max(y, (x + c2)/c0) < x/c0 + c1, false, c0 > 0 && c2 >= c1 * c0) ||
              rewrite(min(y, (x + c2)/c0) < x/c0 + c1, true, c0 > 0 && c2 <= c1 * c0 - c0) ||

              // Same as above with c1 = 0 and the predicates and redundant cases simplified accordingly.
              rewrite(x/c0 < min((x + c2)/c0, y), false, c0 > 0 && c2 < 0) ||
              rewrite(x/c0 < max((x + c2)/c0, y), true, c0 > 0 && c0 <= c2) ||
              rewrite(x/c0 < min(y, (x + c2)/c0), false, c0 > 0 && c2 < 0) ||
              rewrite(x/c0 < max(y, (x + c2)/c0), true, c0 > 0 && c0 <= c2) ||
              rewrite(max((x + c2)/c0, y) < x/c0, false, c0 > 0 && c2 >= 0) ||
              rewrite(min((x + c2)/c0, y) < x/c0, true, c0 > 0 && c2 + c0 <= 0) ||
              rewrite(max(y, (x + c2)/c0) < x/c0, false, c0 > 0 && c2 >= 0) ||
              rewrite(min(y, (x + c2)/c0) < x/c0, true, c0 > 0 && c2 + c0 <= 0) ||

              // Comparison of two mins/maxes that don't cancel when subtracted
              rewrite(min(x, c0) < min(x, c1), false, c0 >= c1) ||
              rewrite(min(x, c0) < min(x, c1) + c2, false, c0 >= c1 + c2) ||
              rewrite(max(x, c0) < max(x, c1), false, c0 >= c1) ||
              rewrite(max(x, c0) < max(x, c1) + c2, false, c0 >= c1 + c2) ||

              // Comparison of aligned ramps can simplify to a comparison of the base
              rewrite(ramp(x * c3 + c2, c1) < broadcast(z * c0),
                      broadcast(x * fold(c3/c0) + fold(c2/c0) < z, lanes),
                      c0 > 0 && (c3 % c0 == 0) &&
                      (c2 % c0) + c1 * (lanes - 1) < c0 &&
                      (c2 % c0) + c1 * (lanes - 1) >= 0) ||
              // c2 = 0
              rewrite(ramp(x * c3, c1) < broadcast(z * c0),
                      broadcast(x * fold(c3/c0) < z, lanes),
                      c0 > 0 && (c3 % c0 == 0) &&
                      c1 * (lanes - 1) < c0 &&
                      c1 * (lanes - 1) >= 0) ||

#if USE_SYNTHESIZED_RULES_V2
 rewrite((w < ((min((x - (y + z)), c0) + w) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((w < ((min(((x + y) - z), c0) + w) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((w < (min(x, (min(min(y, z), w) + c0)) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((x < (y + min(x, 1))), (max(x, 1) <= y)) ||
 rewrite((x < (z + (x + y))), (1 <= (y + z))) ||
 rewrite((x < (z + (y + x))), (1 <= (y + z))) ||
 rewrite((x < ((x + y) + z)), (0 < (y + z))) ||
 rewrite((x < ((y + x) + z)), (1 <= (y + z))) ||
 rewrite((x < (min(x, 1) + y)), (max(x, 1) <= y)) ||
 rewrite((x < (min((min(x, y) + c0), z) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((x < (min(min((x + c0), y), z) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((x < (min(min(min((x + c0), y), z), w) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((x < ((min((x*c0), c1) + c2)/c0)), false, ((((1 <= c1) || (c2 <= 0)) || (((c1 + c2) + 1) <= c0)) && ((max(c2, 0) + 1) <= c0))) ||
 rewrite((x < ((min((x*c0), y) + c1)/c0)), false, ((max(c1, 0) + 1) <= c0)) ||
 rewrite((x < min(y, min(x, (z + min(u, w))))), false) ||
 rewrite((x < min(y, min(x, (z + min(w, u))))), false) ||
 rewrite((x < min(y, min(x, (min(u, w) + z)))), false) ||
 rewrite((x < min(y, min(x, (min(w, u) + z)))), false) ||
 rewrite((x < min(y, min((z + min(u, w)), x))), false) ||
 rewrite((x < min(y, min((z + min(w, u)), x))), false) ||
 rewrite((x < min(y, min((min(u, w) + z), x))), false) ||
 rewrite((x < min(y, min((min(w, u) + z), x))), false) ||
 rewrite((x < min(z, (min(x, y) + c0))), false, (c0 <= 0)) ||
 rewrite((x < min(z, min(x, y))), false) ||
 rewrite((x < min(z, min(y, x))), false) ||
 rewrite((x < min((min(x, y) + c0), z)), false, (c0 <= 0)) ||
 rewrite((x < min(min(x, y), z)), false) ||
 rewrite((x < min(min(y, x), z)), false) ||
 rewrite((x < min(min(min(x, y), (z + w)), c0)), false) ||
 rewrite((y < (x + min(y, 1))), (max(y, 1) <= x)) ||
 rewrite((y < (min(x, (min(y, z) + c0)) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((y < (min(x, (min((y + c0), z) + c1)) + c2)), false, (((c0 + c1) + c2) <= 0)) ||
 rewrite((y < (min(y, 1) + x)), (max(y, 1) <= x)) ||
 rewrite((y < (min((min(x, c0) + y), z) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((y < (min((min(x, y) + c0), z) + c0)), false, (c0 <= 0)) ||
 rewrite((y < (min((min(x, y) + c0), z) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((y < (min(min(x, (y + c0)), z) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((y < (min(min(min(x, (y + c0)), z), w) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((y < (max(min(x, (y + c0)), z) + c1)), ((y + (1 - c1)) <= z), ((c0 + c1) <= 0)) ||
 rewrite((y < min(u, min(min(z, min(x, y)), (w*c0)))), false) ||
 rewrite((y < min(u, min(min(z, min(y, x)), (w*c0)))), false) ||
 rewrite((y < min(u, min(min(min(x, y), z), (w*c0)))), false) ||
 rewrite((y < min(u, min(min(min(y, x), z), (w*c0)))), false) ||
 rewrite((y < min(w, min(z, min(x, y)))), false) ||
 rewrite((y < min(w, min(z, min(y, x)))), false) ||
 rewrite((y < min(w, min(min(x, y), z))), false) ||
 rewrite((y < min(w, min(min(y, x), z))), false) ||
 rewrite((y < min(z, (y + (min(x, c0)*c1)))), false, ((1 <= c1) && (c0 <= 0))) ||
 rewrite((y < min(z, (((min(x, c0)*c1) + y) + c2))), false, ((((1 <= c0) || (c0 <= -1)) || (c2 <= 0)) && (((1 <= c1) && (((c0*c1) + c2) <= (min(c1, 0)*2))) && (min((max((c0*c1), 0) + c2), c0) <= 0)))) ||
 rewrite((y < min(z, ((min(x, c0)*c1) + y))), false, ((1 <= c1) && (c0 <= 0))) ||
 rewrite((y < min(z, min(u, min(min(x, y), (w + z))))), false) ||
 rewrite((y < min(z, min(u, min(min(x, y), (z + w))))), false) ||
 rewrite((y < min(z, min(u, min(min(y, x), (w + z))))), false) ||
 rewrite((y < min(z, min(u, min(min(y, x), (z + w))))), false) ||
 rewrite((y < min(z, min(x, y))), false) ||
 rewrite((y < min(z, min(y, x))), false) ||
 rewrite((y < min(z, min(min(min(x, y), (w + z)), u))), false) ||
 rewrite((y < min(z, min(min(min(x, y), (z + w)), u))), false) ||
 rewrite((y < min(z, min(min(min(y, x), (w + z)), u))), false) ||
 rewrite((y < min(z, min(min(min(y, x), (z + w)), u))), false) ||
 rewrite((y < min((y + (min(x, c0)*c1)), z)), false, ((1 <= c1) && (c0 <= 0))) ||
 rewrite((y < min((((min(x, c0)*c1) + y) + c2), z)), false, ((((1 <= c0) || (c0 <= -1)) || (c2 <= 0)) && (((1 <= c1) && (((c0*c1) + c2) <= (min(c1, 0)*2))) && (min((max((c0*c1), 0) + c2), c0) <= 0)))) ||
 rewrite((y < min(((min(x, c0)*c1) + y), z)), false, ((1 <= c1) && (c0 <= 0))) ||
 rewrite((y < min(min(u, min(min(x, y), (w + z))), z)), false) ||
 rewrite((y < min(min(u, min(min(x, y), (z + w))), z)), false) ||
 rewrite((y < min(min(u, min(min(y, x), (w + z))), z)), false) ||
 rewrite((y < min(min(u, min(min(y, x), (z + w))), z)), false) ||
 rewrite((y < min(min(w, min(z, min(x, y))), (u*c0))), false) ||
 rewrite((y < min(min(w, min(z, min(y, x))), (u*c0))), false) ||
 rewrite((y < min(min(w, min(min(x, y), z)), (u*c0))), false) ||
 rewrite((y < min(min(w, min(min(y, x), z)), (u*c0))), false) ||
 rewrite((y < min(min(x, y), z)), false) ||
 rewrite((y < min(min(y, x), z)), false) ||
 rewrite((y < min(min(z, min(x, y)), w)), false) ||
 rewrite((y < min(min(z, min(y, x)), w)), false) ||
 rewrite((y < min(min(min(x, y), z), w)), false) ||
 rewrite((y < min(min(min(y, x), z), w)), false) ||
 rewrite((y < min(min(min(z, min(x, y)), w), (u*c0))), false) ||
 rewrite((y < min(min(min(z, min(x, y)), (w*c0)), u)), false) ||
 rewrite((y < min(min(min(z, min(y, x)), w), (u*c0))), false) ||
 rewrite((y < min(min(min(z, min(y, x)), (w*c0)), u)), false) ||
 rewrite((y < min(min(min(min(x, y), z), w), (u*c0))), false) ||
 rewrite((y < min(min(min(min(x, y), z), (w*c0)), u)), false) ||
 rewrite((y < min(min(min(min(x, y), (w + z)), u), z)), false) ||
 rewrite((y < min(min(min(min(x, y), (z + w)), u), z)), false) ||
 rewrite((y < min(min(min(min(y, x), z), w), (u*c0))), false) ||
 rewrite((y < min(min(min(min(y, x), z), (w*c0)), u)), false) ||
 rewrite((y < min(min(min(min(y, x), (w + z)), u), z)), false) ||
 rewrite((y < min(min(min(min(y, x), (z + w)), u), z)), false) ||
 rewrite((y < max(z, min(x, y))), (y < z)) ||
 rewrite((y < max(z, min(y, x))), (y < z)) ||
 rewrite((y < max(min(x, y), z)), (y < z)) ||
 rewrite((y < max(min(y, x), z)), (y < z)) ||
 rewrite((z < (min(x, y) + min(w, (z - x)))), false) ||
 rewrite((z < (min(x, y) + min(w, (z - y)))), false) ||
 rewrite((z < (min(x, y) + min((z - x), w))), false) ||
 rewrite((z < (min(x, y) + min((z - y), w))), false) ||
 rewrite((z < (min(x, (min(y, z) + c0)) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((z < (min(x, (min(y, (z + c0)) + c1)) + c2)), false, (((c0 + c1) + c2) <= 0)) ||
 rewrite((z < (min(y, x) + min(w, (z - x)))), false) ||
 rewrite((z < (min(y, x) + min(w, (z - y)))), false) ||
 rewrite((z < (min(y, x) + min((z - x), w))), false) ||
 rewrite((z < (min(y, x) + min((z - y), w))), false) ||
 rewrite((z < (min((min((x*y), c0) + z), w) + c1)), false, (((((c0 + c1) <= 0) && ((min(c1, 0) + c0) <= 1)) && ((min(c0, 0) + c1) <= 1)) && (min(c0, c1) <= 0))) ||
 rewrite((z < min(x, (min(y, z) + c0))), false, (c0 <= 0)) ||
 rewrite((z < min((min(y, z) + c0), x)), false, (c0 <= 0)) ||
 rewrite((z < select((x < y), z, min(w, z))), false) ||
 rewrite((z < select((x < y), z, min(z, w))), false) ||
 rewrite(((u + (w*(x + y))) < (w*(z + (x + y)))), (u < (w*z))) ||
 rewrite(((u + (w*(x + y))) < (w*(z + (y + x)))), (u < (w*z))) ||
 rewrite(((u + (w*(x + y))) < (w*((x + y) + z))), (u < (w*z))) ||
 rewrite(((u + (w*(x + y))) < (w*((y + x) + z))), (u < (w*z))) ||
 rewrite(((u + (w*(x + y))) < ((z + (x + y))*w)), (u < (w*z))) ||
 rewrite(((u + (w*(x + y))) < ((z + (y + x))*w)), (u < (w*z))) ||
 rewrite(((u + (w*(x + y))) < (((x + y) + z)*w)), (u < (w*z))) ||
 rewrite(((u + (w*(x + y))) < (((y + x) + z)*w)), (u < (w*z))) ||
 rewrite(((u + (w*(y + x))) < (w*(z + (x + y)))), (u < (w*z))) ||
 rewrite(((u + (w*(y + x))) < (w*(z + (y + x)))), (u < (w*z))) ||
 rewrite(((u + (w*(y + x))) < (w*((x + y) + z))), (u < (w*z))) ||
 rewrite(((u + (w*(y + x))) < (w*((y + x) + z))), (u < (w*z))) ||
 rewrite(((u + (w*(y + x))) < ((z + (x + y))*w)), (u < (w*z))) ||
 rewrite(((u + (w*(y + x))) < ((z + (y + x))*w)), (u < (w*z))) ||
 rewrite(((u + (w*(y + x))) < (((x + y) + z)*w)), (u < (w*z))) ||
 rewrite(((u + (w*(y + x))) < (((y + x) + z)*w)), (u < (w*z))) ||
 rewrite(((u + ((x + y)*w)) < (w*(z + (x + y)))), (u < (w*z))) ||
 rewrite(((u + ((x + y)*w)) < (w*(z + (y + x)))), (u < (w*z))) ||
 rewrite(((u + ((x + y)*w)) < (w*((x + y) + z))), (u < (w*z))) ||
 rewrite(((u + ((x + y)*w)) < (w*((y + x) + z))), (u < (w*z))) ||
 rewrite(((u + ((x + y)*w)) < ((z + (x + y))*w)), (u < (w*z))) ||
 rewrite(((u + ((x + y)*w)) < ((z + (y + x))*w)), (u < (w*z))) ||
 rewrite(((u + ((x + y)*w)) < (((x + y) + z)*w)), (u < (w*z))) ||
 rewrite(((u + ((x + y)*w)) < (((y + x) + z)*w)), (u < (w*z))) ||
 rewrite(((u + ((y + x)*w)) < (w*(z + (x + y)))), (u < (w*z))) ||
 rewrite(((u + ((y + x)*w)) < (w*(z + (y + x)))), (u < (w*z))) ||
 rewrite(((u + ((y + x)*w)) < (w*((x + y) + z))), (u < (w*z))) ||
 rewrite(((u + ((y + x)*w)) < (w*((y + x) + z))), (u < (w*z))) ||
 rewrite(((u + ((y + x)*w)) < ((z + (x + y))*w)), (u < (w*z))) ||
 rewrite(((u + ((y + x)*w)) < ((z + (y + x))*w)), (u < (w*z))) ||
 rewrite(((u + ((y + x)*w)) < (((x + y) + z)*w)), (u < (w*z))) ||
 rewrite(((u + ((y + x)*w)) < (((y + x) + z)*w)), (u < (w*z))) ||
 rewrite(((w + y) < (min(x, (min(y, z) + w)) + c0)), false, (c0 <= 0)) ||
 rewrite(((w + y) < min(x, (w + min(y, z)))), false) ||
 rewrite(((w + y) < min(x, (w + min(z, y)))), false) ||
 rewrite(((w + y) < min(x, (min(y, z) + w))), false) ||
 rewrite(((w + y) < min(x, (min(z, y) + w))), false) ||
 rewrite(((w + y) < min((w + min(y, z)), x)), false) ||
 rewrite(((w + y) < min((w + min(z, y)), x)), false) ||
 rewrite(((w + y) < min((min(y, z) + w), x)), false) ||
 rewrite(((w + y) < min((min(z, y) + w), x)), false) ||
 rewrite(((w + z) < ((x + (y + z)) + c0)), ((w + (1 - c0)) <= (x + y))) ||
 rewrite(((w + z) < min(x, (w + min(y, z)))), false) ||
 rewrite(((w + z) < min(x, (w + min(z, y)))), false) ||
 rewrite(((w + z) < min(x, (min(y, z) + w))), false) ||
 rewrite(((w + z) < min(x, (min(z, y) + w))), false) ||
 rewrite(((w + z) < min((w + min(y, z)), x)), false) ||
 rewrite(((w + z) < min((w + min(z, y)), x)), false) ||
 rewrite(((w + z) < min((min(y, z) + w), x)), false) ||
 rewrite(((w + z) < min((min(z, y) + w), x)), false) ||
 rewrite(((w + (x*z)) < (z*(x + y))), (w < (y*z))) ||
 rewrite(((w + (x*z)) < (z*(y + x))), (w < (y*z))) ||
 rewrite(((w + (x*z)) < ((x + y)*z)), (w < (y*z))) ||
 rewrite(((w + (x*z)) < ((y + x)*z)), (w < (y*z))) ||
 rewrite(((w + (y*z)) < (((x + y)*z) + 1)), (w <= (x*z))) ||
 rewrite(((w + (z*x)) < (z*(x + y))), (w < (y*z))) ||
 rewrite(((w + (z*x)) < (z*(y + x))), (w < (y*z))) ||
 rewrite(((w + (z*x)) < ((x + y)*z)), (w < (y*z))) ||
 rewrite(((w + (z*x)) < ((y + x)*z)), (w < (y*z))) ||
 rewrite(((w + (z*y)) < (((x + y)*z) + 1)), (w <= (x*z))) ||
 rewrite(((x + y) < (w + (z + (y + x)))), (0 < (w + z))) ||
 rewrite(((x + y) < (w + ((y + x) + z))), (0 < (w + z))) ||
 rewrite(((x + y) < ((z + (y + x)) + w)), (0 < (w + z))) ||
 rewrite(((x + y) < (((y + x) + z) + w)), (0 < (w + z))) ||
 rewrite(((x + y) < min(z, (x + y))), false) ||
 rewrite(((x + y) < min(z, (y + x))), false) ||
 rewrite(((x + y) < min((x + y), z)), false) ||
 rewrite(((x + y) < min((y + x), z)), false) ||
 rewrite(((x + z) < (min((min(x, y) + z), w) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + z) < min(w, (z + min(x, y)))), false) ||
 rewrite(((x + z) < min(w, (z + min(y, x)))), false) ||
 rewrite(((x + z) < min(w, (min(x, y) + z))), false) ||
 rewrite(((x + z) < min(w, (min(y, x) + z))), false) ||
 rewrite(((x + z) < min((z + min(x, y)), w)), false) ||
 rewrite(((x + z) < min((z + min(y, x)), w)), false) ||
 rewrite(((x + z) < min((min(x, y) + z), w)), false) ||
 rewrite(((x + z) < min((min(y, x) + z), w)), false) ||
 rewrite(((x + select((z < w), c1, c2)) < min(y, (x + c0))), false, (c0 <= min(c1, c2))) ||
 rewrite(((x + select((z < w), c1, c2)) < min((x + c0), y)), false, (c0 <= min(c1, c2))) ||
 rewrite(((y + w) < (min(x, (min(y, z) + w)) + c0)), false, (c0 <= 0)) ||
 rewrite(((y + w) < min(x, (w + min(y, z)))), false) ||
 rewrite(((y + w) < min(x, (w + min(z, y)))), false) ||
 rewrite(((y + w) < min(x, (min(y, z) + w))), false) ||
 rewrite(((y + w) < min(x, (min(z, y) + w))), false) ||
 rewrite(((y + w) < min((w + min(y, z)), x)), false) ||
 rewrite(((y + w) < min((w + min(z, y)), x)), false) ||
 rewrite(((y + w) < min((min(y, z) + w), x)), false) ||
 rewrite(((y + w) < min((min(z, y) + w), x)), false) ||
 rewrite(((y + x) < (w + (z + (x + y)))), (0 < (w + z))) ||
 rewrite(((y + x) < (w + ((x + y) + z))), (0 < (w + z))) ||
 rewrite(((y + x) < ((z + (x + y)) + w)), (0 < (w + z))) ||
 rewrite(((y + x) < (((x + y) + z) + w)), (0 < (w + z))) ||
 rewrite(((y + x) < min(z, (x + y))), false) ||
 rewrite(((y + x) < min(z, (y + x))), false) ||
 rewrite(((y + x) < min((x + y), z)), false) ||
 rewrite(((y + x) < min((y + x), z)), false) ||
 rewrite(((y + z) < (w + (x + (z + y)))), (0 < (w + x))) ||
 rewrite(((y + z) < (w + ((z + y) + x))), (0 < (w + x))) ||
 rewrite(((y + z) < ((x + (z + y)) + w)), (0 < (w + x))) ||
 rewrite(((y + z) < (((z + y) + x) + w)), (0 < (w + x))) ||
 rewrite(((y + z) < min(w, (z + min(x, y)))), false) ||
 rewrite(((y + z) < min(w, (z + min(y, x)))), false) ||
 rewrite(((y + z) < min(w, (min(x, y) + z))), false) ||
 rewrite(((y + z) < min(w, (min(y, x) + z))), false) ||
 rewrite(((y + z) < min((z + min(x, y)), w)), false) ||
 rewrite(((y + z) < min((z + min(y, x)), w)), false) ||
 rewrite(((y + z) < min((min(x, y) + z), w)), false) ||
 rewrite(((y + z) < min((min(y, x) + z), w)), false) ||
 rewrite(((y + (x*c0)) < (((z + x)*c0) + c1)), (y < ((z*c0) + c1))) ||
 rewrite(((y + ((((x - y) + c0)/c1)*c1)) < x), false, ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite(((y + ((((x - y) + c0)/c1)*c1)) < x), false, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
 rewrite(((y + (((x - y)/c1)*c1)) < (x + c0)), false, (((c0 + -1) <= c1) && ((c0 + c1) <= 1))) ||
 rewrite(((y + (((x - y)/c1)*c1)) < (x + c0)), false, ((0 <= c1) && ((c0 + c1) <= 1))) ||
 rewrite(((y + min(x, c1)) < (x + c0)), ((y + ((c1 - c0) + 1)) <= max(x, c1))) ||
 rewrite(((y + max(w, x)) < min(z, (x + y))), false) ||
 rewrite(((y + max(w, x)) < min(z, (y + x))), false) ||
 rewrite(((y + max(w, x)) < min((x + y), z)), false) ||
 rewrite(((y + max(w, x)) < min((y + x), z)), false) ||
 rewrite(((y + max(w, z)) < min(x, (y + z))), false) ||
 rewrite(((y + max(w, z)) < min(x, (z + y))), false) ||
 rewrite(((y + max(w, z)) < min((y + z), x)), false) ||
 rewrite(((y + max(w, z)) < min((z + y), x)), false) ||
 rewrite(((y + max(x, w)) < min(z, (x + y))), false) ||
 rewrite(((y + max(x, w)) < min(z, (y + x))), false) ||
 rewrite(((y + max(x, w)) < min((x + y), z)), false) ||
 rewrite(((y + max(x, w)) < min((y + x), z)), false) ||
 rewrite(((y + max(z, w)) < min(x, (y + z))), false) ||
 rewrite(((y + max(z, w)) < min(x, (z + y))), false) ||
 rewrite(((y + max(z, w)) < min((y + z), x)), false) ||
 rewrite(((y + max(z, w)) < min((z + y), x)), false) ||
 rewrite(((y + select((z < w), c0, c1)) < min(x, y)), false, (0 <= min(c0, c1))) ||
 rewrite(((y + select((z < w), c0, c1)) < min(y, x)), false, (0 <= min(c0, c1))) ||
 rewrite(((y + select((z < w), c1, c2)) < (min(x, y) + c0)), false, (((c0 + -1) <= c2) && (c0 <= min(c1, c2)))) ||
 rewrite(((y + select((z < w), c1, c2)) < min(x, (y + c0))), false, (c0 <= min(c1, c2))) ||
 rewrite(((y + select((z < w), c1, c2)) < min((y + c0), x)), false, (c0 <= min(c1, c2))) ||
 rewrite(((z + w) < ((x + (y + z)) + c0)), ((w + (1 - c0)) <= (x + y))) ||
 rewrite(((z + w) < min(x, (w + min(y, z)))), false) ||
 rewrite(((z + w) < min(x, (w + min(z, y)))), false) ||
 rewrite(((z + w) < min(x, (min(y, z) + w))), false) ||
 rewrite(((z + w) < min(x, (min(z, y) + w))), false) ||
 rewrite(((z + w) < min((w + min(y, z)), x)), false) ||
 rewrite(((z + w) < min((w + min(z, y)), x)), false) ||
 rewrite(((z + w) < min((min(y, z) + w), x)), false) ||
 rewrite(((z + w) < min((min(z, y) + w), x)), false) ||
 rewrite(((z + x) < (min((min(x, y) + z), w) + c0)), false, (c0 <= 0)) ||
 rewrite(((z + x) < min(w, (z + min(x, y)))), false) ||
 rewrite(((z + x) < min(w, (z + min(y, x)))), false) ||
 rewrite(((z + x) < min(w, (min(x, y) + z))), false) ||
 rewrite(((z + x) < min(w, (min(y, x) + z))), false) ||
 rewrite(((z + x) < min((z + min(x, y)), w)), false) ||
 rewrite(((z + x) < min((z + min(y, x)), w)), false) ||
 rewrite(((z + x) < min((min(x, y) + z), w)), false) ||
 rewrite(((z + x) < min((min(y, x) + z), w)), false) ||
 rewrite(((z + y) < (w + (x + (y + z)))), (0 < (w + x))) ||
 rewrite(((z + y) < (w + ((y + z) + x))), (0 < (w + x))) ||
 rewrite(((z + y) < ((x + (y + z)) + w)), (0 < (w + x))) ||
 rewrite(((z + y) < (((y + z) + x) + w)), (0 < (w + x))) ||
 rewrite(((z + y) < min(w, (z + min(x, y)))), false) ||
 rewrite(((z + y) < min(w, (z + min(y, x)))), false) ||
 rewrite(((z + y) < min(w, (min(x, y) + z))), false) ||
 rewrite(((z + y) < min(w, (min(y, x) + z))), false) ||
 rewrite(((z + y) < min((z + min(x, y)), w)), false) ||
 rewrite(((z + y) < min((z + min(y, x)), w)), false) ||
 rewrite(((z + y) < min((min(x, y) + z), w)), false) ||
 rewrite(((z + y) < min((min(y, x) + z), w)), false) ||
 rewrite(((z + (x*c0)) < (((x + y)*c0) + c1)), ((z + (1 - c1)) <= (y*c0))) ||
 rewrite(((z + (y*c0)) < (((x + y)*c0) + c1)), ((z + (1 - c1)) <= (x*c0))) ||
 rewrite((((w*(x + y)) + u) < (w*(z + (x + y)))), (u < (w*z))) ||
 rewrite((((w*(x + y)) + u) < (w*(z + (y + x)))), (u < (w*z))) ||
 rewrite((((w*(x + y)) + u) < (w*((x + y) + z))), (u < (w*z))) ||
 rewrite((((w*(x + y)) + u) < (w*((y + x) + z))), (u < (w*z))) ||
 rewrite((((w*(x + y)) + u) < ((z + (x + y))*w)), (u < (w*z))) ||
 rewrite((((w*(x + y)) + u) < ((z + (y + x))*w)), (u < (w*z))) ||
 rewrite((((w*(x + y)) + u) < (((x + y) + z)*w)), (u < (w*z))) ||
 rewrite((((w*(x + y)) + u) < (((y + x) + z)*w)), (u < (w*z))) ||
 rewrite((((w*(y + x)) + u) < (w*(z + (x + y)))), (u < (w*z))) ||
 rewrite((((w*(y + x)) + u) < (w*(z + (y + x)))), (u < (w*z))) ||
 rewrite((((w*(y + x)) + u) < (w*((x + y) + z))), (u < (w*z))) ||
 rewrite((((w*(y + x)) + u) < (w*((y + x) + z))), (u < (w*z))) ||
 rewrite((((w*(y + x)) + u) < ((z + (x + y))*w)), (u < (w*z))) ||
 rewrite((((w*(y + x)) + u) < ((z + (y + x))*w)), (u < (w*z))) ||
 rewrite((((w*(y + x)) + u) < (((x + y) + z)*w)), (u < (w*z))) ||
 rewrite((((w*(y + x)) + u) < (((y + x) + z)*w)), (u < (w*z))) ||
 rewrite((((x*c0) + y) < (((z + x)*c0) + c1)), (y < ((z*c0) + c1))) ||
 rewrite((((x*c0) + z) < (((x + y)*c0) + c1)), ((z + (1 - c1)) <= (y*c0))) ||
 rewrite((((x*z) + w) < (z*(x + y))), (w < (y*z))) ||
 rewrite((((x*z) + w) < (z*(y + x))), (w < (y*z))) ||
 rewrite((((x*z) + w) < ((x + y)*z)), (w < (y*z))) ||
 rewrite((((x*z) + w) < ((y + x)*z)), (w < (y*z))) ||
 rewrite((((y*c0) + z) < (((x + y)*c0) + c1)), ((z + (1 - c1)) <= (x*c0))) ||
 rewrite((((y*z) + w) < (((x + y)*z) + 1)), (w <= (x*z))) ||
 rewrite((((z*x) + w) < (z*(x + y))), (w < (y*z))) ||
 rewrite((((z*x) + w) < (z*(y + x))), (w < (y*z))) ||
 rewrite((((z*x) + w) < ((x + y)*z)), (w < (y*z))) ||
 rewrite((((z*x) + w) < ((y + x)*z)), (w < (y*z))) ||
 rewrite((((z*y) + w) < (((x + y)*z) + 1)), (w <= (x*z))) ||
 rewrite(((((x + y)*w) + u) < (w*(z + (y + x)))), (u < (w*z))) ||
 rewrite(((((x + y)*w) + u) < (w*((y + x) + z))), (u < (w*z))) ||
 rewrite(((((x + y)*w) + u) < ((z + (y + x))*w)), (u < (w*z))) ||
 rewrite(((((x + y)*w) + u) < (((y + x) + z)*w)), (u < (w*z))) ||
 rewrite(((((y + x)*w) + u) < (w*(z + (x + y)))), (u < (w*z))) ||
 rewrite(((((y + x)*w) + u) < (w*((x + y) + z))), (u < (w*z))) ||
 rewrite(((((y + x)*w) + u) < ((z + (x + y))*w)), (u < (w*z))) ||
 rewrite(((((y + x)*w) + u) < (((x + y) + z)*w)), (u < (w*z))) ||
 rewrite(((((((x - y) + c0)/c1)*c1) + y) < x), false, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
 rewrite(((((((x - y) + c0)/c1)*c1) + y) < x), false, ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite((((((x - y)/c1)*c1) + y) < (x + c0)), false, (((c0 + -1) <= c1) && ((c0 + c1) <= 1))) ||
 rewrite((((((x - y)/c1)*c1) + y) < (x + c0)), false, ((0 <= c1) && ((c0 + c1) <= 1))) ||
 rewrite(((min(x, c0) + ((max(x, c0)/c1)*c1)) < x), false, (((1 <= c0) || (c1 <= 1)) && (((-1 <= c1) || (1 <= c0)) && ((-1 <= (c0 + c1)) && ((max(c1, 1) + -1) <= c0))))) ||
 rewrite(((min(x, c1) + y) < (x + c0)), ((y + ((c1 - c0) + 1)) <= max(x, c1))) ||
 rewrite(((min((x*c0), c1) + (y*c0)) < (x*c0)), ((min(x, 0) + y) < x), ((0 <= c1) && ((max(c1, 0) + 1) <= c0))) ||
 rewrite(((min((x*c0), c1) + (y*c0)) < (x*c0)), (y <= max(x, -1)), (((0 <= (c0 + c1)) && (1 <= c0)) && (c1 <= -1))) ||
 rewrite(((max(u, x) + (y + z)) < min(w, (x + (y + z)))), false) ||
 rewrite(((max(u, x) + (y + z)) < min(w, (x + (z + y)))), false) ||
 rewrite(((max(u, x) + (y + z)) < min(w, ((y + z) + x))), false) ||
 rewrite(((max(u, x) + (y + z)) < min(w, ((z + y) + x))), false) ||
 rewrite(((max(u, x) + (y + z)) < min((x + (y + z)), w)), false) ||
 rewrite(((max(u, x) + (y + z)) < min((x + (z + y)), w)), false) ||
 rewrite(((max(u, x) + (y + z)) < min(((y + z) + x), w)), false) ||
 rewrite(((max(u, x) + (y + z)) < min(((z + y) + x), w)), false) ||
 rewrite(((max(u, x) + (z + y)) < min(w, (x + (y + z)))), false) ||
 rewrite(((max(u, x) + (z + y)) < min(w, (x + (z + y)))), false) ||
 rewrite(((max(u, x) + (z + y)) < min(w, ((y + z) + x))), false) ||
 rewrite(((max(u, x) + (z + y)) < min(w, ((z + y) + x))), false) ||
 rewrite(((max(u, x) + (z + y)) < min((x + (y + z)), w)), false) ||
 rewrite(((max(u, x) + (z + y)) < min((x + (z + y)), w)), false) ||
 rewrite(((max(u, x) + (z + y)) < min(((y + z) + x), w)), false) ||
 rewrite(((max(u, x) + (z + y)) < min(((z + y) + x), w)), false) ||
 rewrite(((max(w, x) + y) < min(z, (x + y))), false) ||
 rewrite(((max(w, x) + y) < min(z, (y + x))), false) ||
 rewrite(((max(w, x) + y) < min((x + y), z)), false) ||
 rewrite(((max(w, x) + y) < min((y + x), z)), false) ||
 rewrite(((max(w, z) + y) < min(x, (y + z))), false) ||
 rewrite(((max(w, z) + y) < min(x, (z + y))), false) ||
 rewrite(((max(w, z) + y) < min((y + z), x)), false) ||
 rewrite(((max(w, z) + y) < min((z + y), x)), false) ||
 rewrite(((max(x, u) + (y + z)) < min(w, (x + (y + z)))), false) ||
 rewrite(((max(x, u) + (y + z)) < min(w, (x + (z + y)))), false) ||
 rewrite(((max(x, u) + (y + z)) < min(w, ((y + z) + x))), false) ||
 rewrite(((max(x, u) + (y + z)) < min(w, ((z + y) + x))), false) ||
 rewrite(((max(x, u) + (y + z)) < min((x + (y + z)), w)), false) ||
 rewrite(((max(x, u) + (y + z)) < min((x + (z + y)), w)), false) ||
 rewrite(((max(x, u) + (y + z)) < min(((y + z) + x), w)), false) ||
 rewrite(((max(x, u) + (y + z)) < min(((z + y) + x), w)), false) ||
 rewrite(((max(x, u) + (z + y)) < min(w, (x + (y + z)))), false) ||
 rewrite(((max(x, u) + (z + y)) < min(w, (x + (z + y)))), false) ||
 rewrite(((max(x, u) + (z + y)) < min(w, ((y + z) + x))), false) ||
 rewrite(((max(x, u) + (z + y)) < min(w, ((z + y) + x))), false) ||
 rewrite(((max(x, u) + (z + y)) < min((x + (y + z)), w)), false) ||
 rewrite(((max(x, u) + (z + y)) < min((x + (z + y)), w)), false) ||
 rewrite(((max(x, u) + (z + y)) < min(((y + z) + x), w)), false) ||
 rewrite(((max(x, u) + (z + y)) < min(((z + y) + x), w)), false) ||
 rewrite(((max(x, w) + y) < min(z, (x + y))), false) ||
 rewrite(((max(x, w) + y) < min(z, (y + x))), false) ||
 rewrite(((max(x, w) + y) < min((x + y), z)), false) ||
 rewrite(((max(x, w) + y) < min((y + x), z)), false) ||
 rewrite(((max(z, w) + y) < min(x, (y + z))), false) ||
 rewrite(((max(z, w) + y) < min(x, (z + y))), false) ||
 rewrite(((max(z, w) + y) < min((y + z), x)), false) ||
 rewrite(((max(z, w) + y) < min((z + y), x)), false) ||
 rewrite(((select((z < w), c0, c1) + y) < min(x, y)), false, (0 <= min(c0, c1))) ||
 rewrite(((select((z < w), c0, c1) + y) < min(y, x)), false, (0 <= min(c0, c1))) ||
 rewrite(((select((z < w), c1, c2) + x) < min(y, (x + c0))), false, (c0 <= min(c1, c2))) ||
 rewrite(((select((z < w), c1, c2) + x) < min((x + c0), y)), false, (c0 <= min(c1, c2))) ||
 rewrite(((select((z < w), c1, c2) + y) < (min(x, y) + c0)), false, (((c0 + -1) <= c2) && (c0 <= min(c1, c2)))) ||
 rewrite(((select((z < w), c1, c2) + y) < min(x, (y + c0))), false, (c0 <= min(c1, c2))) ||
 rewrite(((select((z < w), c1, c2) + y) < min((y + c0), x)), false, (c0 <= min(c1, c2))) ||
 rewrite(((w*(u + (y + z))) < (x + (w*(y + z)))), ((u*w) < x)) ||
 rewrite(((w*(u + (y + z))) < (x + (w*(z + y)))), ((u*w) < x)) ||
 rewrite(((w*(u + (y + z))) < (x + ((y + z)*w))), ((u*w) < x)) ||
 rewrite(((w*(u + (y + z))) < (x + ((z + y)*w))), ((u*w) < x)) ||
 rewrite(((w*(u + (y + z))) < ((w*(y + z)) + x)), ((u*w) < x)) ||
 rewrite(((w*(u + (y + z))) < ((w*(z + y)) + x)), ((u*w) < x)) ||
 rewrite(((w*(u + (y + z))) < (((y + z)*w) + x)), ((u*w) < x)) ||
 rewrite(((w*(u + (y + z))) < (((z + y)*w) + x)), ((u*w) < x)) ||
 rewrite(((w*(u + (z + y))) < (x + (w*(y + z)))), ((u*w) < x)) ||
 rewrite(((w*(u + (z + y))) < (x + (w*(z + y)))), ((u*w) < x)) ||
 rewrite(((w*(u + (z + y))) < (x + ((y + z)*w))), ((u*w) < x)) ||
 rewrite(((w*(u + (z + y))) < (x + ((z + y)*w))), ((u*w) < x)) ||
 rewrite(((w*(u + (z + y))) < ((w*(y + z)) + x)), ((u*w) < x)) ||
 rewrite(((w*(u + (z + y))) < ((w*(z + y)) + x)), ((u*w) < x)) ||
 rewrite(((w*(u + (z + y))) < (((y + z)*w) + x)), ((u*w) < x)) ||
 rewrite(((w*(u + (z + y))) < (((z + y)*w) + x)), ((u*w) < x)) ||
 rewrite(((w*(y + z)) < ((x + ((y + z)*w)) + c0)), ((0 - c0) < x)) ||
 rewrite(((w*(z + y)) < ((x + ((y + z)*w)) + c0)), ((0 - c0) < x)) ||
 rewrite(((w*((y + z) + u)) < (x + (w*(y + z)))), ((u*w) < x)) ||
 rewrite(((w*((y + z) + u)) < (x + (w*(z + y)))), ((u*w) < x)) ||
 rewrite(((w*((y + z) + u)) < (x + ((y + z)*w))), ((u*w) < x)) ||
 rewrite(((w*((y + z) + u)) < (x + ((z + y)*w))), ((u*w) < x)) ||
 rewrite(((w*((y + z) + u)) < ((w*(y + z)) + x)), ((u*w) < x)) ||
 rewrite(((w*((y + z) + u)) < ((w*(z + y)) + x)), ((u*w) < x)) ||
 rewrite(((w*((y + z) + u)) < (((y + z)*w) + x)), ((u*w) < x)) ||
 rewrite(((w*((y + z) + u)) < (((z + y)*w) + x)), ((u*w) < x)) ||
 rewrite(((w*((z + y) + u)) < (x + (w*(y + z)))), ((u*w) < x)) ||
 rewrite(((w*((z + y) + u)) < (x + (w*(z + y)))), ((u*w) < x)) ||
 rewrite(((w*((z + y) + u)) < (x + ((y + z)*w))), ((u*w) < x)) ||
 rewrite(((w*((z + y) + u)) < (x + ((z + y)*w))), ((u*w) < x)) ||
 rewrite(((w*((z + y) + u)) < ((w*(y + z)) + x)), ((u*w) < x)) ||
 rewrite(((w*((z + y) + u)) < ((w*(z + y)) + x)), ((u*w) < x)) ||
 rewrite(((w*((z + y) + u)) < (((y + z)*w) + x)), ((u*w) < x)) ||
 rewrite(((w*((z + y) + u)) < (((z + y)*w) + x)), ((u*w) < x)) ||
 rewrite(((y*c0) < min(z, (min(x, y)*c0))), false, (1 <= c0)) ||
 rewrite(((y*c0) < min((min(x, y)*c0), z)), false, (1 <= c0)) ||
 rewrite(((y*min(x, c1)) < min((x*y), c0)), false, ((((1 <= c1) || (c0 <= 0)) || (c1 <= -1)) && (max(c0, 0) <= c1))) ||
 rewrite(((y*min(x, y)) < min((x*y), c0)), false, (c0 <= 1)) ||
 rewrite(((y*min(y, x)) < min((x*y), c0)), false, (c0 <= 1)) ||
 rewrite(((z*(w + y)) < (x + (y*z))), ((w*z) < x)) ||
 rewrite(((z*(w + y)) < (x + (z*y))), ((w*z) < x)) ||
 rewrite(((z*(w + y)) < ((x + (y*z)) + c0)), (((w*z) + (1 - c0)) <= x)) ||
 rewrite(((z*(w + y)) < ((y*z) + x)), ((w*z) < x)) ||
 rewrite(((z*(w + y)) < ((z*y) + x)), ((w*z) < x)) ||
 rewrite(((z*(y + w)) < (x + (y*z))), ((w*z) < x)) ||
 rewrite(((z*(y + w)) < (x + (z*y))), ((w*z) < x)) ||
 rewrite(((z*(y + w)) < ((x + (y*z)) + c0)), (((w*z) + (1 - c0)) <= x)) ||
 rewrite(((z*(y + w)) < ((y*z) + x)), ((w*z) < x)) ||
 rewrite(((z*(y + w)) < ((z*y) + x)), ((w*z) < x)) ||
 rewrite(((z*min(x, y)) < min(u, min(w, (z*min(y, x))))), false) ||
 rewrite(((z*min(x, y)) < min(u, min(w, (min(x, y)*z)))), false) ||
 rewrite(((z*min(x, y)) < min(u, min(w, (min(y, x)*z)))), false) ||
 rewrite(((z*min(x, y)) < min(u, min((z*min(y, x)), w))), false) ||
 rewrite(((z*min(x, y)) < min(u, min((min(x, y)*z), w))), false) ||
 rewrite(((z*min(x, y)) < min(u, min((min(y, x)*z), w))), false) ||
 rewrite(((z*min(x, y)) < min(min(w, (z*min(y, x))), u)), false) ||
 rewrite(((z*min(x, y)) < min(min(w, (min(x, y)*z)), u)), false) ||
 rewrite(((z*min(x, y)) < min(min(w, (min(y, x)*z)), u)), false) ||
 rewrite(((z*min(x, y)) < min(min((z*min(y, x)), w), u)), false) ||
 rewrite(((z*min(x, y)) < min(min((min(x, y)*z), w), u)), false) ||
 rewrite(((z*min(x, y)) < min(min((min(y, x)*z), w), u)), false) ||
 rewrite(((z*min(y, x)) < min(u, min(w, (z*min(x, y))))), false) ||
 rewrite(((z*min(y, x)) < min(u, min(w, (min(x, y)*z)))), false) ||
 rewrite(((z*min(y, x)) < min(u, min(w, (min(y, x)*z)))), false) ||
 rewrite(((z*min(y, x)) < min(u, min((z*min(x, y)), w))), false) ||
 rewrite(((z*min(y, x)) < min(u, min((min(x, y)*z), w))), false) ||
 rewrite(((z*min(y, x)) < min(u, min((min(y, x)*z), w))), false) ||
 rewrite(((z*min(y, x)) < min(min(w, (z*min(x, y))), u)), false) ||
 rewrite(((z*min(y, x)) < min(min(w, (min(x, y)*z)), u)), false) ||
 rewrite(((z*min(y, x)) < min(min(w, (min(y, x)*z)), u)), false) ||
 rewrite(((z*min(y, x)) < min(min((z*min(x, y)), w), u)), false) ||
 rewrite(((z*min(y, x)) < min(min((min(x, y)*z), w), u)), false) ||
 rewrite(((z*min(y, x)) < min(min((min(y, x)*z), w), u)), false) ||
 rewrite((((u + (y + z))*w) < (x + (w*(y + z)))), ((u*w) < x)) ||
 rewrite((((u + (y + z))*w) < (x + (w*(z + y)))), ((u*w) < x)) ||
 rewrite((((u + (y + z))*w) < (x + ((y + z)*w))), ((u*w) < x)) ||
 rewrite((((u + (y + z))*w) < (x + ((z + y)*w))), ((u*w) < x)) ||
 rewrite((((u + (y + z))*w) < ((w*(y + z)) + x)), ((u*w) < x)) ||
 rewrite((((u + (y + z))*w) < ((w*(z + y)) + x)), ((u*w) < x)) ||
 rewrite((((u + (y + z))*w) < (((y + z)*w) + x)), ((u*w) < x)) ||
 rewrite((((u + (y + z))*w) < (((z + y)*w) + x)), ((u*w) < x)) ||
 rewrite((((u + (z + y))*w) < (x + (w*(y + z)))), ((u*w) < x)) ||
 rewrite((((u + (z + y))*w) < (x + (w*(z + y)))), ((u*w) < x)) ||
 rewrite((((u + (z + y))*w) < (x + ((y + z)*w))), ((u*w) < x)) ||
 rewrite((((u + (z + y))*w) < (x + ((z + y)*w))), ((u*w) < x)) ||
 rewrite((((u + (z + y))*w) < ((w*(y + z)) + x)), ((u*w) < x)) ||
 rewrite((((u + (z + y))*w) < ((w*(z + y)) + x)), ((u*w) < x)) ||
 rewrite((((u + (z + y))*w) < (((y + z)*w) + x)), ((u*w) < x)) ||
 rewrite((((u + (z + y))*w) < (((z + y)*w) + x)), ((u*w) < x)) ||
 rewrite((((w + y)*z) < (x + (y*z))), ((w*z) < x)) ||
 rewrite((((w + y)*z) < (x + (z*y))), ((w*z) < x)) ||
 rewrite((((w + y)*z) < ((x + (y*z)) + c0)), (((w*z) + (1 - c0)) <= x)) ||
 rewrite((((w + y)*z) < ((y*z) + x)), ((w*z) < x)) ||
 rewrite((((w + y)*z) < ((z*y) + x)), ((w*z) < x)) ||
 rewrite((((y + w)*z) < (x + (y*z))), ((w*z) < x)) ||
 rewrite((((y + w)*z) < (x + (z*y))), ((w*z) < x)) ||
 rewrite((((y + w)*z) < ((x + (y*z)) + c0)), (((w*z) + (1 - c0)) <= x)) ||
 rewrite((((y + w)*z) < ((y*z) + x)), ((w*z) < x)) ||
 rewrite((((y + w)*z) < ((z*y) + x)), ((w*z) < x)) ||
 rewrite((((z + y)*w) < ((x + ((y + z)*w)) + c0)), ((0 - c0) < x)) ||
 rewrite((((z + (x*c1))*c2) < (y + (x*c0))), ((z*c2) < y), ((c1*c2) == c0)) ||
 rewrite((((z + (x*c1))*c2) < ((x*c0) + y)), ((z*c2) < y), ((c1*c2) == c0)) ||
 rewrite((((z + (y*c1))*c1) < (x + (y*c0))), ((z*c1) < x), ((1 <= c0) && ((c1*c1) == c0))) ||
 rewrite((((z + (y*c1))*c1) < ((y*c0) + x)), ((z*c1) < x), ((1 <= c0) && ((c1*c1) == c0))) ||
 rewrite(((((y + z) + u)*w) < (x + (w*(z + y)))), ((u*w) < x)) ||
 rewrite(((((y + z) + u)*w) < (x + ((z + y)*w))), ((u*w) < x)) ||
 rewrite(((((y + z) + u)*w) < ((w*(z + y)) + x)), ((u*w) < x)) ||
 rewrite(((((y + z) + u)*w) < (((z + y)*w) + x)), ((u*w) < x)) ||
 rewrite(((((z + y) + u)*w) < (x + (w*(y + z)))), ((u*w) < x)) ||
 rewrite(((((z + y) + u)*w) < (x + ((y + z)*w))), ((u*w) < x)) ||
 rewrite(((((z + y) + u)*w) < ((w*(y + z)) + x)), ((u*w) < x)) ||
 rewrite(((((z + y) + u)*w) < (((y + z)*w) + x)), ((u*w) < x)) ||
 rewrite(((((x*c1) + z)*c2) < (y + (x*c0))), ((z*c2) < y), ((c1*c2) == c0)) ||
 rewrite(((((x*c1) + z)*c2) < ((x*c0) + y)), ((z*c2) < y), ((c1*c2) == c0)) ||
 rewrite(((((y*c1) + z)*c2) < (x + (y*c0))), ((z*c2) < x), ((c1*c2) == c0)) ||
 rewrite(((((y*c1) + z)*c2) < ((y*c0) + x)), ((z*c2) < x), ((c1*c2) == c0)) ||
 rewrite(((((x + c0)/c1)*c2) < min((x*c1), c3)), (x <= c2), ((((((((((((((((((-3 <= ((c0 + c1) + c2)) || (-2 <= (c0 + c1))) || (-2 <= (c0 + c2))) || (-2 <= ((c0 + c1) + c2))) || (-1 <= c0)) || (1 <= c2)) || ((((c0 + c1)*c2) + (c1 + c2)) <= (c1*c1))) || ((((c0 + c1)*c2) + (c1 + c2)) <= (c1*c3))) || ((((c0 + c1)*c2) + (c1 + c2)) <= (((c2 + 2)*c1)*c1))) || (((((c0 + c1) + c2)*c2) + ((c2*2) + c1)) <= (c1*c3))) || (((((c0 + c1) + c2)*c2) + ((c2*2) + c1)) <= (((c2 + 2)*c1)*c1))) || (((((c0 + c1)*c2) + ((c2*2) + c1)) + 1) <= (c1*c1))) || (((((c0 + c1)*c2) + ((c2*2) + c1)) + 1) <= (c1*c3))) || ((((((c0 + c1) + c2)*c2) + ((c2*3) + c1)) + 1) <= (c1*c3))) || ((((((c0 + c1) + c2)*c2) + ((c2*3) + c1)) + 1) <= (((c2 + 2)*c1)*c1))) || ((((c1 + 1)*c1) + -2) <= (c0 + c2))) || ((((c1 + 1)*c1) + -2) <= ((c0 + c1) + c2))) && (((((((((1 <= max(((c1*c1) + ((c0 + -1)*c2)), c2)) || (c0 <= 1)) || (c0 <= (c1*c1))) || ((((c1*c2)*c1) + 1) <= ((c0 + c2)*c2))) || ((c0 + c1) <= 0)) || ((c0 + c2) <= 0)) || (((c0 + c2) + 1) <= (c1*c1))) || (((c0 + c1) + c2) <= -1)) && (((((((-1 <= c2) || ((((((c0 + c1) + c2)*c2) + ((c2*2) + c1)) + 1) <= (c1*c3))) || ((((((c0 + c1) + c2)*c2) + ((c2*2) + c1)) + 1) <= (((c2 + 1)*c1)*c1))) || (((c1*c1) + -1) <= (c0 + c2))) || (((c1*c1) + -1) <= ((c0 + c1) + c2))) || (((c1*c1) + -2) <= ((c0 + c1) + c2))) && ((((((-1 <= (c0 + c1)) || (0 <= c0)) || (1 <= c2)) || ((((c0 + c1)*c2) + (c1 + c2)) <= -1)) || (((((c0 + c1)*c2) + (c1 + c2)) + 1) <= (c1*c3))) && ((((1 <= c2) || (((c1*c3) + 1) <= ((c0 + -1)*c2))) || (((c1*c3) + 1) <= ((c0 + c2)*c2))) && (max(c1, c2) <= -1))))))) ||
 rewrite(((min(x, c1)*y) < min((x*y), c0)), false, ((((1 <= c1) || (c0 <= 0)) || (c1 <= -1)) && (max(c0, 0) <= c1))) ||
 rewrite(((min(x, c2)*c0) < min((x*c0), c1)), false, ((((((c0 <= -1) || (c1 <= 1)) || (c1 <= c2)) || (c1 <= (c0*c2))) || (c2 <= 0)) && ((((((-1 <= c2) || (0 <= ((c2 + 1)*c0))) && (0 <= c2)) || (c0 <= -1)) || (c1 <= (c0*c2))) && ((((1 <= c2) || (c0 <= -1)) || (c1 <= 0)) || (c2 <= -1))))) ||
 rewrite(((min(x, y)*c0) < min((x*c0), c1)), (y < min(x, 0)), ((1 <= min((min(c1, 0) + c0), ((c0*2) + c1))) && (c1 <= 0))) ||
 rewrite(((min(x, y)*y) < min((x*y), c0)), false, (c0 <= 1)) ||
 rewrite(((min(x, y)*z) < min(u, min(w, (z*min(x, y))))), false) ||
 rewrite(((min(x, y)*z) < min(u, min(w, (z*min(y, x))))), false) ||
 rewrite(((min(x, y)*z) < min(u, min(w, (min(y, x)*z)))), false) ||
 rewrite(((min(x, y)*z) < min(u, min((z*min(x, y)), w))), false) ||
 rewrite(((min(x, y)*z) < min(u, min((z*min(y, x)), w))), false) ||
 rewrite(((min(x, y)*z) < min(u, min((min(y, x)*z), w))), false) ||
 rewrite(((min(x, y)*z) < min(min(w, (z*min(x, y))), u)), false) ||
 rewrite(((min(x, y)*z) < min(min(w, (z*min(y, x))), u)), false) ||
 rewrite(((min(x, y)*z) < min(min(w, (min(y, x)*z)), u)), false) ||
 rewrite(((min(x, y)*z) < min(min((z*min(x, y)), w), u)), false) ||
 rewrite(((min(x, y)*z) < min(min((z*min(y, x)), w), u)), false) ||
 rewrite(((min(x, y)*z) < min(min((min(y, x)*z), w), u)), false) ||
 rewrite(((min(y, x)*y) < min((x*y), c0)), false, (c0 <= 1)) ||
 rewrite(((min(y, x)*z) < min(u, min(w, (z*min(x, y))))), false) ||
 rewrite(((min(y, x)*z) < min(u, min(w, (z*min(y, x))))), false) ||
 rewrite(((min(y, x)*z) < min(u, min(w, (min(x, y)*z)))), false) ||
 rewrite(((min(y, x)*z) < min(u, min((z*min(x, y)), w))), false) ||
 rewrite(((min(y, x)*z) < min(u, min((z*min(y, x)), w))), false) ||
 rewrite(((min(y, x)*z) < min(u, min((min(x, y)*z), w))), false) ||
 rewrite(((min(y, x)*z) < min(min(w, (z*min(x, y))), u)), false) ||
 rewrite(((min(y, x)*z) < min(min(w, (z*min(y, x))), u)), false) ||
 rewrite(((min(y, x)*z) < min(min(w, (min(x, y)*z)), u)), false) ||
 rewrite(((min(y, x)*z) < min(min((z*min(x, y)), w), u)), false) ||
 rewrite(((min(y, x)*z) < min(min((z*min(y, x)), w), u)), false) ||
 rewrite(((min(y, x)*z) < min(min((min(x, y)*z), w), u)), false) ||
 rewrite(((min(((x + c0)/c1), y)*c1) < x), ((y*c1) < x), (((1 <= c1) || (c0 <= -1)) && ((c1 <= -1) || ((c1 + -1) <= c0)))) ||
 rewrite(((max(x, z)*c0) < min(y, (x*c0))), false, (1 <= c0)) ||
 rewrite(((max(x, z)*c0) < min((x*c0), y)), false, (1 <= c0)) ||
 rewrite(((max(z, x)*c0) < min(y, (x*c0))), false, (1 <= c0)) ||
 rewrite(((max(z, x)*c0) < min((x*c0), y)), false, (1 <= c0)) ||
 rewrite(((x/c0) < (min(x, y)/c0)), false, (1 <= c0)) ||
 rewrite(((x/c0) < (min(y, x)/c0)), false, (1 <= c0)) ||
 rewrite(((y/c0) < (min(x, y)/c0)), false, (0 <= c0)) ||
 rewrite(((y/c0) < (min(y, x)/c0)), false, (0 <= c0)) ||
 rewrite(((y/c2) < (((min(x, c0) + y) + c1)/c2)), false, (((max((c0 + c1), 0) + 1) <= c2) && ((c0 + c1) <= 0))) ||
 rewrite((((x + c2)/c1) < (y + ((x + c0)/c1))), (c2 < y), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
 rewrite((((x + c2)/c1) < (((x + c0)/c1) + y)), (c2 < y), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
 rewrite((((x + z)/c0) < ((z + min(x, y))/c0)), false, (1 <= c0)) ||
 rewrite((((x + z)/c0) < ((z + min(y, x))/c0)), false, (1 <= c0)) ||
 rewrite((((x + z)/c0) < ((min(x, y) + z)/c0)), false, (1 <= c0)) ||
 rewrite((((x + z)/c0) < ((min(y, x) + z)/c0)), false, (1 <= c0)) ||
 rewrite((((y + c0)/c1) < ((min(x, y) + c0)/c1)), false, (1 <= c1)) ||
 rewrite((((y + c2)/c1) < (x + ((y + c0)/c1))), (c2 < x), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
 rewrite((((y + c2)/c1) < (x + ((y + c0)/c1))), (c0 < x), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1 + 1)*c0) == c2))) ||
 rewrite((((y + c2)/c1) < (((y + c0)/c1) + x)), (c2 < x), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
 rewrite((((y + c2)/c1) < (((y + c0)/c1) + x)), (c0 < x), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1 + 1)*c0) == c2))) ||
 rewrite((((y + z)/c0) < ((z + min(x, y))/c0)), false, (1 <= c0)) ||
 rewrite((((y + z)/c0) < ((z + min(y, x))/c0)), false, (1 <= c0)) ||
 rewrite((((y + z)/c0) < ((min(x, y) + z)/c0)), false, (1 <= c0)) ||
 rewrite((((y + z)/c0) < ((min(y, x) + z)/c0)), false, (1 <= c0)) ||
 rewrite((((y + min(x, c0))/c1) < (min(z, (y + min(x, c0)))/c1)), false, (1 <= c1)) ||
 rewrite((((y + min(x, c0))/c1) < (min(z, (min(x, c0) + y))/c1)), false, (1 <= c1)) ||
 rewrite((((y + min(x, c0))/c1) < (min((y + min(x, c0)), z)/c1)), false, (1 <= c1)) ||
 rewrite((((y + min(x, c0))/c1) < (min((min(x, c0) + y), z)/c1)), false, (1 <= c1)) ||
 rewrite((((z + x)/c0) < ((z + min(x, y))/c0)), false, (1 <= c0)) ||
 rewrite((((z + x)/c0) < ((z + min(y, x))/c0)), false, (1 <= c0)) ||
 rewrite((((z + x)/c0) < ((min(x, y) + z)/c0)), false, (1 <= c0)) ||
 rewrite((((z + x)/c0) < ((min(y, x) + z)/c0)), false, (1 <= c0)) ||
 rewrite((((z + y)/c0) < ((z + min(x, y))/c0)), false, (1 <= c0)) ||
 rewrite((((z + y)/c0) < ((z + min(y, x))/c0)), false, (1 <= c0)) ||
 rewrite((((z + y)/c0) < ((min(x, y) + z)/c0)), false, (1 <= c0)) ||
 rewrite((((z + y)/c0) < ((min(y, x) + z)/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(x, y))/c0) < (min(w, (z + min(y, x)))/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(x, y))/c0) < (min(w, (min(x, y) + z))/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(x, y))/c0) < (min(w, (min(y, x) + z))/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(x, y))/c0) < (min((z + min(y, x)), w)/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(x, y))/c0) < (min((min(x, y) + z), w)/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(x, y))/c0) < (min((min(y, x) + z), w)/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(y, x))/c0) < (min(w, (z + min(x, y)))/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(y, x))/c0) < (min(w, (min(x, y) + z))/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(y, x))/c0) < (min(w, (min(y, x) + z))/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(y, x))/c0) < (min((z + min(x, y)), w)/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(y, x))/c0) < (min((min(x, y) + z), w)/c0)), false, (1 <= c0)) ||
 rewrite((((z + min(y, x))/c0) < (min((min(y, x) + z), w)/c0)), false, (1 <= c0)) ||
 rewrite(((((x*c0) + c3)/c2) < (((x*c0) + c1)/c2)), false, (((1 <= c2) || (c3 <= c1)) && ((c1 <= c3) || (c2 <= -1)))) ||
 rewrite((((min(x, c0) + y)/c1) < (min(z, (y + min(x, c0)))/c1)), false, (1 <= c1)) ||
 rewrite((((min(x, c0) + y)/c1) < (min(z, (min(x, c0) + y))/c1)), false, (1 <= c1)) ||
 rewrite((((min(x, c0) + y)/c1) < (min((y + min(x, c0)), z)/c1)), false, (1 <= c1)) ||
 rewrite((((min(x, c0) + y)/c1) < (min((min(x, c0) + y), z)/c1)), false, (1 <= c1)) ||
 rewrite((((min(x, y) + z)/c0) < (min(w, (z + min(x, y)))/c0)), false, (1 <= c0)) ||
 rewrite((((min(x, y) + z)/c0) < (min(w, (z + min(y, x)))/c0)), false, (1 <= c0)) ||
 rewrite((((min(x, y) + z)/c0) < (min(w, (min(y, x) + z))/c0)), false, (1 <= c0)) ||
 rewrite((((min(x, y) + z)/c0) < (min((z + min(x, y)), w)/c0)), false, (1 <= c0)) ||
 rewrite((((min(x, y) + z)/c0) < (min((z + min(y, x)), w)/c0)), false, (1 <= c0)) ||
 rewrite((((min(x, y) + z)/c0) < (min((min(y, x) + z), w)/c0)), false, (1 <= c0)) ||
 rewrite((((min(y, x) + z)/c0) < (min(w, (z + min(x, y)))/c0)), false, (1 <= c0)) ||
 rewrite((((min(y, x) + z)/c0) < (min(w, (z + min(y, x)))/c0)), false, (1 <= c0)) ||
 rewrite((((min(y, x) + z)/c0) < (min(w, (min(x, y) + z))/c0)), false, (1 <= c0)) ||
 rewrite((((min(y, x) + z)/c0) < (min((z + min(x, y)), w)/c0)), false, (1 <= c0)) ||
 rewrite((((min(y, x) + z)/c0) < (min((z + min(y, x)), w)/c0)), false, (1 <= c0)) ||
 rewrite((((min(y, x) + z)/c0) < (min((min(x, y) + z), w)/c0)), false, (1 <= c0)) ||
 rewrite((((c2 - x)/c1) < ((c0 - x)/c1)), false, (((1 <= c1) || (c2 <= c0)) && ((c0 <= c2) || (c1 <= -1)))) ||
 rewrite((((x*c2)/c3) < ((x*c0)/c1)), false, ((1 <= c1) && ((c1*c2) == (c0*c3)))) ||
 rewrite(((max(x, y)/c0) < (x/c0)), false, (1 <= c0)) ||
 rewrite(((max(y, x)/c0) < (x/c0)), false, (1 <= c0)) ||
 rewrite((min(w, z) < min(w, min(z, min(x, y)))), false) ||
 rewrite((min(w, z) < min(w, min(z, min(y, x)))), false) ||
 rewrite((min(w, z) < min(w, min(min(x, y), z))), false) ||
 rewrite((min(w, z) < min(w, min(min(y, x), z))), false) ||
 rewrite((min(w, z) < min(min(z, min(x, y)), w)), false) ||
 rewrite((min(w, z) < min(min(z, min(y, x)), w)), false) ||
 rewrite((min(w, z) < min(min(min(x, y), z), w)), false) ||
 rewrite((min(w, z) < min(min(min(y, x), z), w)), false) ||
 rewrite((min(w, min(x, (min(y, z) + c0))) < y), true, (c0 <= -1)) ||
 rewrite((min(w, min(z, (min(x, y) + c0))) < x), true, (c0 <= -1)) ||
 rewrite((min(w, min((min(x, y) + c0), z)) < x), true, (c0 <= -1)) ||
 rewrite((min(w, min((min(y, z) + c0), x)) < y), true, (c0 <= -1)) ||
 rewrite((min(x, c1) < min(min(x, y), c0)), false, (c0 < (c1 + 1))) ||
 rewrite((min(x, c1) < min(min(min(x, y), z), c0)), false, (c0 <= c1)) ||
 rewrite((min(x, c2) < (((min(x, c0) + c1)/c2)*c2)), false, (((((c2 <= -1) || ((c0 + c1) <= 1)) || ((c0 + c1) <= c2)) || (((c0 + c1) + 1) <= (c2*2))) && ((((1 <= c2) || ((c0 + c1) <= -1)) || ((c0 + c1) <= c2)) && (c1 <= 0)))) ||
 rewrite((min(x, c3) < ((((x + c0)/c1)*c1) + c2)), (max((c3 - (c0 + c2)), ((c3 - ((c0 + c1) + c2)) + -1)) < x), ((((c1 <= -1) && ((c3 + -1) <= (max((c3 - (c0 + c2)), ((c3 - ((c0 + c1) + c2)) + -1)) + ((c0 + c1) + c2)))) && ((c0 + c2) <= 0)) && (-1 <= c1))) ||
 rewrite((min(x, c3) < (((min(x, c0)/c1)*c1) + c2)), false, (((((c1 <= -1) || ((c2 + -1) <= c3)) || (c2 <= (c1 + c3))) || (((0 <= c0) || ((c0 + c2) <= c3)) && ((1 <= c0) || (c0 <= -1)))) && (((((1 <= c1) || ((c2 + -1) <= c3)) || ((c1 + c2) <= c3)) || (((0 <= c0) || ((c0 + c2) <= c3)) && ((1 <= c0) || (c0 <= -1)))) && ((((1 <= max(c0, c1)) || (c0 <= -1)) || (c1 <= -1)) && ((((c0 <= 0) || (((c0 + c2) + -2) <= c3)) || ((((1 <= c1) || ((c2 + -1) <= c3)) || ((c1 + c2) <= c3)) && (((c1 <= -1) || ((c2 + -1) <= c3)) || (c2 <= (c1 + c3))))) && ((((1 <= c0) || (c0 <= -1)) || (c2 <= c3)) && ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c0 <= 0) || ((c0 + c2) <= c3)) && (c2 <= 0))))))))) ||
 rewrite((min(x, c3) < (((min(x, c0)/c1)*c1) + c2)), false, (((((1 <= max(c0, c1)) || (c0 <= -1)) || ((c2 + -1) <= c3)) || ((c1 + c2) <= c3)) && ((((((0 <= c0) || (1 <= c1)) || ((c2 + -1) <= c3)) || ((c0 + c2) <= c3)) || ((c1 + c2) <= c3)) && (((((c1 <= -1) || ((c2 + -1) <= c3)) || (c2 <= (c1 + c3))) || ((((c0 + c2) <= c3) || ((min(c1, 1) + -1) <= c0)) && (((0 <= c0) || ((c0 + c2) <= c3)) && ((1 <= c0) || (c0 <= -1))))) && ((((1 <= max(c0, c1)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= c0) || (c0 <= -1)) || (c2 <= c3)) && ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && ((((((1 <= c1) || ((c2 + -1) <= c3)) || ((c1 + c2) <= c3)) && (((c1 <= -1) || ((c2 + -1) <= c3)) || (c2 <= (c1 + c3)))) || (((c0 + c2) + -2) <= c3)) && (((c0 <= 0) || ((c0 + c2) <= c3)) && (c2 <= 0)))))))))) ||
 rewrite((min(x, y) < min(x, y)), false) ||
 rewrite((min(x, y) < min(x, (y + c0))), false, (c0 <= 0)) ||
 rewrite((min(x, y) < min(y, x)), false) ||
 rewrite((min(x, y) < min((y + c0), x)), false, (c0 <= 0)) ||
 rewrite((min(x, z) < (min(min((x + c0), y), z) + c1)), false, ((max(c0, 0) + c1) <= 0)) ||
 rewrite((min(x, z) < min(x, y)), (z < min(x, y))) ||
 rewrite((min(x, z) < min(y, x)), (z < min(x, y))) ||
 rewrite((min(x, (y + c1)) < (min((x + c0), y) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((min(x, (y + c2)) < (min(x, (y + c0)) + c1)), false, ((c1 <= 0) && ((c0 + c1) <= c2))) ||
 rewrite((min(x, (z + c0)) < min(z, min(x, y))), false, (0 <= c0)) ||
 rewrite((min(x, (z + c0)) < min(z, min(y, x))), false, (0 <= c0)) ||
 rewrite((min(x, (z + c0)) < min(min(x, y), z)), false, (0 <= c0)) ||
 rewrite((min(x, (z + c0)) < min(min(y, x), z)), false, (0 <= c0)) ||
 rewrite((min(x, (z + c1)) < min(min(x, y), (z + c0))), false, (c0 <= c1)) ||
 rewrite((min(x, (z + c1)) < min(min(y, x), (z + c0))), false, (c0 <= c1)) ||
 rewrite((min(y, c1) < min(min(x, y), c0)), false, (c0 < (c1 + 1))) ||
 rewrite((min(y, c1) < min(min(min(x, y), z), c0)), false, (c0 <= c1)) ||
 rewrite((min(y, c3) < (min(min(x, (y + c0)), c1) + c2)), false, (((c0 + c2) <= 0) && ((c1 + c2) < (c3 + 1)))) ||
 rewrite((min(y, x) < min(x, y)), false) ||
 rewrite((min(y, x) < min(x, (y + c0))), false, (c0 <= 0)) ||
 rewrite((min(y, x) < min(y, x)), false) ||
 rewrite((min(y, x) < min((y + c0), x)), false, (c0 <= 0)) ||
 rewrite((min(y, z) < (min(x, y) + c0)), (z < (min(x, y) + c0)), (c0 <= 0)) ||
 rewrite((min(y, z) < min(x, y)), (z < min(x, y))) ||
 rewrite((min(y, z) < min(y, x)), (z < min(x, y))) ||
 rewrite((min(y, (x + c0)) < min(w, min(z, min(x, y)))), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(w, min(z, min(y, x)))), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(w, min(min(x, y), z))), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(w, min(min(y, x), z))), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(z, min(x, y))), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(z, min(y, x))), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(min(x, y), z)), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(min(y, x), z)), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(min(z, min(x, y)), w)), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(min(z, min(y, x)), w)), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(min(min(x, y), z), w)), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(min(min(y, x), z), w)), false, (0 <= c0)) ||
 rewrite((min(y, (x + c1)) < (min(x, (y + c0)) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((min(y, (x + c1)) < min(min(x, y), c0)), false, (0 <= c1)) ||
 rewrite((min(y, (x + c2)) < (min((x + c0), y) + c1)), false, ((c1 <= 0) && ((c0 + c1) < (c2 + 1)))) ||
 rewrite((min(y, (z + c0)) < min(w, min(z, min(x, y)))), false, (0 <= c0)) ||
 rewrite((min(y, (z + c0)) < min(w, min(z, min(y, x)))), false, (0 <= c0)) ||
 rewrite((min(y, (z + c0)) < min(w, min(min(x, y), z))), false, (0 <= c0)) ||
 rewrite((min(y, (z + c0)) < min(w, min(min(y, x), z))), false, (0 <= c0)) ||
 rewrite((min(y, (z + c0)) < min(min(z, min(x, y)), w)), false, (0 <= c0)) ||
 rewrite((min(y, (z + c0)) < min(min(z, min(y, x)), w)), false, (0 <= c0)) ||
 rewrite((min(y, (z + c0)) < min(min(min(x, y), z), w)), false, (0 <= c0)) ||
 rewrite((min(y, (z + c0)) < min(min(min(y, x), z), w)), false, (0 <= c0)) ||
 rewrite((min(y, (z + c1)) < min(min(x, y), (z + c0))), false, (c0 <= c1)) ||
 rewrite((min(y, (z + c1)) < min(min(y, x), (z + c0))), false, (c0 <= c1)) ||
 rewrite((min(y, (((x + c1)/c2)*c2)) < (x + c0)), ((y + (1 - c0)) <= x), (((c0 + -1) <= (c1 + c2)) && (((c0 + c2) + -1) <= c1))) ||
 rewrite((min(z, w) < min(w, min(z, min(x, y)))), false) ||
 rewrite((min(z, w) < min(w, min(z, min(y, x)))), false) ||
 rewrite((min(z, w) < min(w, min(min(x, y), z))), false) ||
 rewrite((min(z, w) < min(w, min(min(y, x), z))), false) ||
 rewrite((min(z, w) < min(min(z, min(x, y)), w)), false) ||
 rewrite((min(z, w) < min(min(z, min(y, x)), w)), false) ||
 rewrite((min(z, w) < min(min(min(x, y), z), w)), false) ||
 rewrite((min(z, w) < min(min(min(y, x), z), w)), false) ||
 rewrite((min(z, x) < (min(min((x + c0), y), z) + c1)), false, ((max(c0, 0) + c1) <= 0)) ||
 rewrite((min(z, x) < min(x, y)), (z < min(x, y))) ||
 rewrite((min(z, x) < min(y, x)), (z < min(x, y))) ||
 rewrite((min(z, y) < (min(x, y) + c0)), (z < (min(x, y) + c0)), (c0 <= 0)) ||
 rewrite((min(z, y) < min(x, y)), (z < min(x, y))) ||
 rewrite((min(z, y) < min(y, x)), (z < min(x, y))) ||
 rewrite((min(z, (w + y)) < (x + y)), (min((z - y), w) < x)) ||
 rewrite((min(z, (w + y)) < (y + x)), (min((z - y), w) < x)) ||
 rewrite((min(z, (y + c0)) < min(x, y)), (z < min(x, y)), (0 <= c0)) ||
 rewrite((min(z, (y + c0)) < min(y, x)), (z < min(x, y)), (0 <= c0)) ||
 rewrite((min(z, (y + c1)) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 < (min(c1, 1) + 1)))) ||
 rewrite((min(z, (y + c1)) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 <= min(c1, 1)))) ||
 rewrite((min(z, (y + w)) < (x + y)), (min((z - y), w) < x)) ||
 rewrite((min(z, (y + w)) < (y + x)), (min((z - y), w) < x)) ||
 rewrite((min(z, min(x, y)) < x), (min(y, z) < x)) ||
 rewrite((min(z, min(x, (y + c0))) < y), true, (c0 <= -1)) ||
 rewrite((min(z, min(y, x)) < x), (min(y, z) < x)) ||
 rewrite((min(z, min(y, (x + c0))) < x), true, (c0 <= -1)) ||
 rewrite((min(z, min((x + c0), y)) < x), true, (c0 <= -1)) ||
 rewrite((min(z, min((y + c0), x)) < y), true, (c0 <= -1)) ||
 rewrite((min((w + y), z) < (x + y)), (min((z - y), w) < x)) ||
 rewrite((min((w + y), z) < (y + x)), (min((z - y), w) < x)) ||
 rewrite((min((x + c0), y) < min(w, min(z, min(x, y)))), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(w, min(z, min(y, x)))), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(w, min(min(x, y), z))), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(w, min(min(y, x), z))), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(z, min(x, y))), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(z, min(y, x))), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(min(x, y), z)), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(min(y, x), z)), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(min(z, min(x, y)), w)), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(min(z, min(y, x)), w)), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(min(min(x, y), z), w)), false, (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(min(min(y, x), z), w)), false, (0 <= c0)) ||
 rewrite((min((x + c1), y) < min(min(x, y), c0)), false, (0 <= c1)) ||
 rewrite((min((x + c2), y) < (min((x + c0), y) + c1)), false, ((c1 <= 0) && ((c0 + c1) < (c2 + 1)))) ||
 rewrite((min((y + c0), z) < min(x, y)), (z < min(x, y)), (0 <= c0)) ||
 rewrite((min((y + c0), z) < min(y, x)), (z < min(x, y)), (0 <= c0)) ||
 rewrite((min((y + c1), z) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 < (min(c1, 1) + 1)))) ||
 rewrite((min((y + c1), z) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 <= min(c1, 1)))) ||
 rewrite((min((y + c2), x) < (min(x, (y + c0)) + c1)), false, ((c1 <= 0) && ((c0 + c1) <= c2))) ||
 rewrite((min((y + w), z) < (x + y)), (min((z - y), w) < x)) ||
 rewrite((min((y + w), z) < (y + x)), (min((z - y), w) < x)) ||
 rewrite((min((z + c0), y) < min(w, min(z, min(x, y)))), false, (0 <= c0)) ||
 rewrite((min((z + c0), y) < min(w, min(z, min(y, x)))), false, (0 <= c0)) ||
 rewrite((min((z + c0), y) < min(w, min(min(x, y), z))), false, (0 <= c0)) ||
 rewrite((min((z + c0), y) < min(w, min(min(y, x), z))), false, (0 <= c0)) ||
 rewrite((min((z + c0), y) < min(min(z, min(x, y)), w)), false, (0 <= c0)) ||
 rewrite((min((z + c0), y) < min(min(z, min(y, x)), w)), false, (0 <= c0)) ||
 rewrite((min((z + c0), y) < min(min(min(x, y), z), w)), false, (0 <= c0)) ||
 rewrite((min((z + c0), y) < min(min(min(y, x), z), w)), false, (0 <= c0)) ||
 rewrite((min((z + c1), x) < min(min(x, y), (z + c0))), false, (c0 <= c1)) ||
 rewrite((min((z + c1), x) < min(min(y, x), (z + c0))), false, (c0 <= c1)) ||
 rewrite((min((z + c1), y) < min(min(x, y), (z + c0))), false, (c0 <= c1)) ||
 rewrite((min((z + c1), y) < min(min(y, x), (z + c0))), false, (c0 <= c1)) ||
 rewrite((min((x*c1), c3) < ((min(x, c0)*c1) + c2)), false, ((((1 <= c0) || (c0 <= -1)) || (c2 <= (min(c1, 0) + c3))) && (((c0 <= 0) || (c2 <= c3)) && (((1 <= c1) && (c2 <= 0)) && (((c0*c1) + c2) <= ((min(c1, 0)*2) + c3)))))) ||
 rewrite((min((x*c2), c3) < (min((x*c0), c1)*c1)), false, (((((c0 <= -1) || (c1 <= 1)) || (c1 <= c0)) || (c2 <= -1)) && ((((1 <= max(c0, c2)) || (c1 <= 1)) || ((c0 + c1) <= 0)) && ((((c0 <= -1) || (c2 <= 1)) || (c2 <= (c0*c1))) && (((min(c0, c2) <= -1) || ((c1*c1) <= c2)) && (((1 <= max(c0, c2)) || (((c1*c1) + c2) <= 0)) && ((((-1 <= c2) || (1 <= c0)) || ((c0*c1) <= c2)) && (((1 <= c2) || (c0 <= -1)) && (((1 <= c0) || (c2 <= -1)) && (((0 <= c3) && (1 <= c1)) && ((c1*c1) <= c3))))))))))) ||
 rewrite((min((x*c2), c3) < (min((x*c0), y)*c1)), ((1 - min(c0, 0)) <= min(x, y)), (((((((0 <= c3) && (1 <= c0)) && (1 <= c2)) && (c2 <= (c0*c1))) && ((c3 + 1) <= (c0*c1))) && ((c3 + 1) <= (c1*2))) && ((max(c3, 0) + 1) <= c1))) ||
 rewrite((min((x*c2), c3) < (min((x*c0), y)*c1)), (0 < min(x, y)), (((0 <= c3) || (c0 <= -1)) && ((((((0 <= (min((c0*c1), c1) + c3)) && (1 <= c0)) && (1 <= c2)) && (c2 <= (c0*c1))) && ((c3 + 1) <= (c0*c1))) && ((max(c3, 0) + 1) <= c1)))) ||
 rewrite((min((((x + c1)/c2)*c2), y) < (x + c0)), ((y + (1 - c0)) <= x), (((c0 + -1) <= (c1 + c2)) && (((c0 + c2) + -1) <= c1))) ||
 rewrite((min(min(x, y), c0) < (x + c1)), true, (1 <= c1)) ||
 rewrite((min(min(x, y), z) < x), (min(y, z) < x)) ||
 rewrite((min(min(x, (y + c0)), z) < y), true, (c0 <= -1)) ||
 rewrite((min(min(x, (min(y, z) + c0)), w) < y), true, (c0 <= -1)) ||
 rewrite((min(min(y, x), z) < x), (min(y, z) < x)) ||
 rewrite((min(min(y, (x + c0)), z) < x), true, (c0 <= -1)) ||
 rewrite((min(min(z, (min(x, y) + c0)), w) < x), true, (c0 <= -1)) ||
 rewrite((min(min((x + c0), y), z) < x), true, (c0 <= -1)) ||
 rewrite((min(min((y + c0), x), z) < y), true, (c0 <= -1)) ||
 rewrite((min(min((min(x, y) + c0), z), w) < x), true, (c0 <= -1)) ||
 rewrite((min(min((min(y, z) + c0), x), w) < y), true, (c0 <= -1)) ||
 rewrite((max(u, (w + z)) < (w + min(z, min(x, y)))), false) ||
 rewrite((max(u, (w + z)) < (w + min(z, min(y, x)))), false) ||
 rewrite((max(u, (w + z)) < (w + min(min(x, y), z))), false) ||
 rewrite((max(u, (w + z)) < (w + min(min(y, x), z))), false) ||
 rewrite((max(u, (w + z)) < (min(z, min(x, y)) + w)), false) ||
 rewrite((max(u, (w + z)) < (min(z, min(y, x)) + w)), false) ||
 rewrite((max(u, (w + z)) < (min(min(x, y), z) + w)), false) ||
 rewrite((max(u, (w + z)) < (min(min(y, x), z) + w)), false) ||
 rewrite((max(u, (z + w)) < (w + min(z, min(x, y)))), false) ||
 rewrite((max(u, (z + w)) < (w + min(z, min(y, x)))), false) ||
 rewrite((max(u, (z + w)) < (w + min(min(x, y), z))), false) ||
 rewrite((max(u, (z + w)) < (w + min(min(y, x), z))), false) ||
 rewrite((max(u, (z + w)) < (min(z, min(x, y)) + w)), false) ||
 rewrite((max(u, (z + w)) < (min(z, min(y, x)) + w)), false) ||
 rewrite((max(u, (z + w)) < (min(min(x, y), z) + w)), false) ||
 rewrite((max(u, (z + w)) < (min(min(y, x), z) + w)), false) ||
 rewrite((max(u, (z + max(w, (x + y)))) < (z + (x + y))), false) ||
 rewrite((max(u, (z + max(w, (x + y)))) < (z + (y + x))), false) ||
 rewrite((max(u, (z + max(w, (x + y)))) < ((x + y) + z)), false) ||
 rewrite((max(u, (z + max(w, (x + y)))) < ((y + x) + z)), false) ||
 rewrite((max(u, (z + max(w, (y + x)))) < (z + (x + y))), false) ||
 rewrite((max(u, (z + max(w, (y + x)))) < (z + (y + x))), false) ||
 rewrite((max(u, (z + max(w, (y + x)))) < ((x + y) + z)), false) ||
 rewrite((max(u, (z + max(w, (y + x)))) < ((y + x) + z)), false) ||
 rewrite((max(u, (z + max((x + y), w))) < (z + (x + y))), false) ||
 rewrite((max(u, (z + max((x + y), w))) < (z + (y + x))), false) ||
 rewrite((max(u, (z + max((x + y), w))) < ((x + y) + z)), false) ||
 rewrite((max(u, (z + max((x + y), w))) < ((y + x) + z)), false) ||
 rewrite((max(u, (z + max((y + x), w))) < (z + (x + y))), false) ||
 rewrite((max(u, (z + max((y + x), w))) < (z + (y + x))), false) ||
 rewrite((max(u, (z + max((y + x), w))) < ((x + y) + z)), false) ||
 rewrite((max(u, (z + max((y + x), w))) < ((y + x) + z)), false) ||
 rewrite((max(u, (max(w, z) + (x + y))) < (z + (x + y))), false) ||
 rewrite((max(u, (max(w, z) + (x + y))) < (z + (y + x))), false) ||
 rewrite((max(u, (max(w, z) + (x + y))) < ((x + y) + z)), false) ||
 rewrite((max(u, (max(w, z) + (x + y))) < ((y + x) + z)), false) ||
 rewrite((max(u, (max(w, z) + (y + x))) < (z + (x + y))), false) ||
 rewrite((max(u, (max(w, z) + (y + x))) < (z + (y + x))), false) ||
 rewrite((max(u, (max(w, z) + (y + x))) < ((x + y) + z)), false) ||
 rewrite((max(u, (max(w, z) + (y + x))) < ((y + x) + z)), false) ||
 rewrite((max(u, (max(w, (x + y)) + z)) < (z + (x + y))), false) ||
 rewrite((max(u, (max(w, (x + y)) + z)) < (z + (y + x))), false) ||
 rewrite((max(u, (max(w, (x + y)) + z)) < ((x + y) + z)), false) ||
 rewrite((max(u, (max(w, (x + y)) + z)) < ((y + x) + z)), false) ||
 rewrite((max(u, (max(w, (y + x)) + z)) < (z + (x + y))), false) ||
 rewrite((max(u, (max(w, (y + x)) + z)) < (z + (y + x))), false) ||
 rewrite((max(u, (max(w, (y + x)) + z)) < ((x + y) + z)), false) ||
 rewrite((max(u, (max(w, (y + x)) + z)) < ((y + x) + z)), false) ||
 rewrite((max(u, (max(z, w) + (x + y))) < (z + (x + y))), false) ||
 rewrite((max(u, (max(z, w) + (x + y))) < (z + (y + x))), false) ||
 rewrite((max(u, (max(z, w) + (x + y))) < ((x + y) + z)), false) ||
 rewrite((max(u, (max(z, w) + (x + y))) < ((y + x) + z)), false) ||
 rewrite((max(u, (max(z, w) + (y + x))) < (z + (x + y))), false) ||
 rewrite((max(u, (max(z, w) + (y + x))) < (z + (y + x))), false) ||
 rewrite((max(u, (max(z, w) + (y + x))) < ((x + y) + z)), false) ||
 rewrite((max(u, (max(z, w) + (y + x))) < ((y + x) + z)), false) ||
 rewrite((max(u, (max((x + y), w) + z)) < (z + (x + y))), false) ||
 rewrite((max(u, (max((x + y), w) + z)) < (z + (y + x))), false) ||
 rewrite((max(u, (max((x + y), w) + z)) < ((x + y) + z)), false) ||
 rewrite((max(u, (max((x + y), w) + z)) < ((y + x) + z)), false) ||
 rewrite((max(u, (max((y + x), w) + z)) < (z + (x + y))), false) ||
 rewrite((max(u, (max((y + x), w) + z)) < (z + (y + x))), false) ||
 rewrite((max(u, (max((y + x), w) + z)) < ((x + y) + z)), false) ||
 rewrite((max(u, (max((y + x), w) + z)) < ((y + x) + z)), false) ||
 rewrite((max(w, (x + max(y, z))) < (x + y)), false) ||
 rewrite((max(w, (x + max(y, z))) < (y + x)), false) ||
 rewrite((max(w, (x + max(z, y))) < (x + y)), false) ||
 rewrite((max(w, (x + max(z, y))) < (y + x)), false) ||
 rewrite((max(w, (y + z)) < (z + (y + min(x, c0)))), false, (c0 <= 0)) ||
 rewrite((max(w, (y + z)) < (z + (min(x, c0) + y))), false, (c0 <= 0)) ||
 rewrite((max(w, (y + z)) < (z + min(x, y))), false) ||
 rewrite((max(w, (y + z)) < (z + min(y, x))), false) ||
 rewrite((max(w, (y + z)) < ((y + min(x, c0)) + z)), false, (c0 <= 0)) ||
 rewrite((max(w, (y + z)) < ((min(x, c0) + y) + z)), false, (c0 <= 0)) ||
 rewrite((max(w, (y + z)) < (min(x, y) + z)), false) ||
 rewrite((max(w, (y + z)) < (min(y, x) + z)), false) ||
 rewrite((max(w, (y + max(x, z))) < (x + y)), false) ||
 rewrite((max(w, (y + max(x, z))) < (y + x)), false) ||
 rewrite((max(w, (y + max(z, x))) < (x + y)), false) ||
 rewrite((max(w, (y + max(z, x))) < (y + x)), false) ||
 rewrite((max(w, (z + y)) < (z + (y + min(x, c0)))), false, (c0 <= 0)) ||
 rewrite((max(w, (z + y)) < (z + (min(x, c0) + y))), false, (c0 <= 0)) ||
 rewrite((max(w, (z + y)) < (z + min(x, y))), false) ||
 rewrite((max(w, (z + y)) < (z + min(y, x))), false) ||
 rewrite((max(w, (z + y)) < ((y + min(x, c0)) + z)), false, (c0 <= 0)) ||
 rewrite((max(w, (z + y)) < ((min(x, c0) + y) + z)), false, (c0 <= 0)) ||
 rewrite((max(w, (z + y)) < (min(x, y) + z)), false) ||
 rewrite((max(w, (z + y)) < (min(y, x) + z)), false) ||
 rewrite((max(w, (max(x, z) + y)) < (x + y)), false) ||
 rewrite((max(w, (max(x, z) + y)) < (y + x)), false) ||
 rewrite((max(w, (max(y, z) + x)) < (x + y)), false) ||
 rewrite((max(w, (max(y, z) + x)) < (y + x)), false) ||
 rewrite((max(w, (max(z, x) + y)) < (x + y)), false) ||
 rewrite((max(w, (max(z, x) + y)) < (y + x)), false) ||
 rewrite((max(w, (max(z, y) + x)) < (x + y)), false) ||
 rewrite((max(w, (max(z, y) + x)) < (y + x)), false) ||
 rewrite((max(x, c0) < ((max((x*c1), c2) + c3)/c1)), true, (((c1 <= (c2 + c3)) || (c2 <= 0)) && (((1 <= c2) || (((c0 + 1)*c1) <= c3)) && (((1 <= c1) && (c1 <= c3)) && (((c0 + 1)*c1) <= (c2 + c3)))))) ||
 rewrite((max(x, z) < min(x, y)), false) ||
 rewrite((max(x, z) < min(y, x)), false) ||
 rewrite((max(x, z) < max(x, y)), (max(x, z) < y)) ||
 rewrite((max(x, z) < max(y, x)), (max(x, z) < y)) ||
 rewrite((max(y, z) < (max(x, y) + c0)), ((max(y, z) + max((0 - min(c0, 0)), (1 - c0))) <= x), (c0 <= 0)) ||
 rewrite((max(y, z) < min(x, y)), false) ||
 rewrite((max(y, z) < min(y, x)), false) ||
 rewrite((max(y, z) < max(x, y)), (max(y, z) < x)) ||
 rewrite((max(y, z) < max(y, x)), (max(y, z) < x)) ||
 rewrite((max(y, (x + c2)) < (max((x + c0), y) + c1)), false, ((c1 <= 0) && ((c0 + c1) <= c2))) ||
 rewrite((max(z, x) < min(x, y)), false) ||
 rewrite((max(z, x) < min(y, x)), false) ||
 rewrite((max(z, x) < max(x, y)), (max(x, z) < y)) ||
 rewrite((max(z, x) < max(y, x)), (max(x, z) < y)) ||
 rewrite((max(z, y) < (max(x, y) + c0)), ((max(y, z) + max((0 - min(c0, 0)), (1 - c0))) <= x), (c0 <= 0)) ||
 rewrite((max(z, y) < min(x, y)), false) ||
 rewrite((max(z, y) < min(y, x)), false) ||
 rewrite((max(z, y) < max(x, y)), (max(y, z) < x)) ||
 rewrite((max(z, y) < max(y, x)), (max(y, z) < x)) ||
 rewrite((max(z, (x + max(w, y))) < (x + y)), false) ||
 rewrite((max(z, (x + max(w, y))) < (y + x)), false) ||
 rewrite((max(z, (x + max(y, w))) < (x + y)), false) ||
 rewrite((max(z, (x + max(y, w))) < (y + x)), false) ||
 rewrite((max(z, (y + max(w, x))) < (x + y)), false) ||
 rewrite((max(z, (y + max(w, x))) < (y + x)), false) ||
 rewrite((max(z, (y + max(x, w))) < (x + y)), false) ||
 rewrite((max(z, (y + max(x, w))) < (y + x)), false) ||
 rewrite((max(z, (max(w, x) + y)) < (x + y)), false) ||
 rewrite((max(z, (max(w, x) + y)) < (y + x)), false) ||
 rewrite((max(z, (max(w, y) + x)) < (x + y)), false) ||
 rewrite((max(z, (max(w, y) + x)) < (y + x)), false) ||
 rewrite((max(z, (max(x, w) + y)) < (x + y)), false) ||
 rewrite((max(z, (max(x, w) + y)) < (y + x)), false) ||
 rewrite((max(z, (max(y, w) + x)) < (x + y)), false) ||
 rewrite((max(z, (max(y, w) + x)) < (y + x)), false) ||
 rewrite((max(z, min(x, (y + c0))) < min(x, (y + c0))), false) ||
 rewrite((max(z, min(x, (y + c0))) < min((y + c0), x)), false) ||
 rewrite((max(z, min((y + c0), x)) < min(x, (y + c0))), false) ||
 rewrite((max(z, min((y + c0), x)) < min((y + c0), x)), false) ||
 rewrite((max(z, max(x, y)) < x), false) ||
 rewrite((max(z, max(y, x)) < x), false) ||
 rewrite((max((w + z), u) < (w + min(z, min(x, y)))), false) ||
 rewrite((max((w + z), u) < (w + min(z, min(y, x)))), false) ||
 rewrite((max((w + z), u) < (w + min(min(x, y), z))), false) ||
 rewrite((max((w + z), u) < (w + min(min(y, x), z))), false) ||
 rewrite((max((w + z), u) < (min(z, min(x, y)) + w)), false) ||
 rewrite((max((w + z), u) < (min(z, min(y, x)) + w)), false) ||
 rewrite((max((w + z), u) < (min(min(x, y), z) + w)), false) ||
 rewrite((max((w + z), u) < (min(min(y, x), z) + w)), false) ||
 rewrite((max((x + c2), y) < (max((x + c0), y) + c1)), false, ((c1 <= 0) && ((c0 + c1) <= c2))) ||
 rewrite((max((x + max(w, y)), z) < (x + y)), false) ||
 rewrite((max((x + max(w, y)), z) < (y + x)), false) ||
 rewrite((max((x + max(y, w)), z) < (x + y)), false) ||
 rewrite((max((x + max(y, w)), z) < (y + x)), false) ||
 rewrite((max((x + max(y, z)), w) < (x + y)), false) ||
 rewrite((max((x + max(y, z)), w) < (y + x)), false) ||
 rewrite((max((x + max(z, y)), w) < (x + y)), false) ||
 rewrite((max((x + max(z, y)), w) < (y + x)), false) ||
 rewrite((max((y + c0), z) < (min(x, y) + c0)), false) ||
 rewrite((max((y + z), w) < (z + (y + min(x, c0)))), false, (c0 <= 0)) ||
 rewrite((max((y + z), w) < (z + (min(x, c0) + y))), false, (c0 <= 0)) ||
 rewrite((max((y + z), w) < (z + min(x, y))), false) ||
 rewrite((max((y + z), w) < (z + min(y, x))), false) ||
 rewrite((max((y + z), w) < ((y + min(x, c0)) + z)), false, (c0 <= 0)) ||
 rewrite((max((y + z), w) < ((min(x, c0) + y) + z)), false, (c0 <= 0)) ||
 rewrite((max((y + z), w) < (min(x, y) + z)), false) ||
 rewrite((max((y + z), w) < (min(y, x) + z)), false) ||
 rewrite((max((y + max(w, x)), z) < (x + y)), false) ||
 rewrite((max((y + max(w, x)), z) < (y + x)), false) ||
 rewrite((max((y + max(x, w)), z) < (x + y)), false) ||
 rewrite((max((y + max(x, w)), z) < (y + x)), false) ||
 rewrite((max((y + max(x, z)), w) < (x + y)), false) ||
 rewrite((max((y + max(x, z)), w) < (y + x)), false) ||
 rewrite((max((y + max(z, x)), w) < (x + y)), false) ||
 rewrite((max((y + max(z, x)), w) < (y + x)), false) ||
 rewrite((max((z + y), w) < (z + (y + min(x, c0)))), false, (c0 <= 0)) ||
 rewrite((max((z + y), w) < (z + (min(x, c0) + y))), false, (c0 <= 0)) ||
 rewrite((max((z + y), w) < (z + min(x, y))), false) ||
 rewrite((max((z + y), w) < (z + min(y, x))), false) ||
 rewrite((max((z + y), w) < ((y + min(x, c0)) + z)), false, (c0 <= 0)) ||
 rewrite((max((z + y), w) < ((min(x, c0) + y) + z)), false, (c0 <= 0)) ||
 rewrite((max((z + y), w) < (min(x, y) + z)), false) ||
 rewrite((max((z + y), w) < (min(y, x) + z)), false) ||
 rewrite((max((z + max(w, (x + y))), u) < (z + (y + x))), false) ||
 rewrite((max((z + max(w, (x + y))), u) < ((y + x) + z)), false) ||
 rewrite((max((z + max(w, (y + x))), u) < (z + (x + y))), false) ||
 rewrite((max((z + max(w, (y + x))), u) < ((x + y) + z)), false) ||
 rewrite((max((z + max((x + y), w)), u) < (z + (y + x))), false) ||
 rewrite((max((z + max((x + y), w)), u) < ((y + x) + z)), false) ||
 rewrite((max((z + max((y + x), w)), u) < (z + (x + y))), false) ||
 rewrite((max((z + max((y + x), w)), u) < ((x + y) + z)), false) ||
 rewrite((max((max(w, x) + y), z) < (x + y)), false) ||
 rewrite((max((max(w, x) + y), z) < (y + x)), false) ||
 rewrite((max((max(w, y) + x), z) < (x + y)), false) ||
 rewrite((max((max(w, y) + x), z) < (y + x)), false) ||
 rewrite((max((max(w, z) + (x + y)), u) < (z + (y + x))), false) ||
 rewrite((max((max(w, z) + (x + y)), u) < ((y + x) + z)), false) ||
 rewrite((max((max(w, z) + (y + x)), u) < (z + (x + y))), false) ||
 rewrite((max((max(w, z) + (y + x)), u) < ((x + y) + z)), false) ||
 rewrite((max((max(w, (x + y)) + z), u) < (z + (y + x))), false) ||
 rewrite((max((max(w, (x + y)) + z), u) < ((y + x) + z)), false) ||
 rewrite((max((max(w, (y + x)) + z), u) < (z + (x + y))), false) ||
 rewrite((max((max(w, (y + x)) + z), u) < ((x + y) + z)), false) ||
 rewrite((max((max(x, w) + y), z) < (x + y)), false) ||
 rewrite((max((max(x, w) + y), z) < (y + x)), false) ||
 rewrite((max((max(x, z) + y), w) < (x + y)), false) ||
 rewrite((max((max(x, z) + y), w) < (y + x)), false) ||
 rewrite((max((max(y, w) + x), z) < (x + y)), false) ||
 rewrite((max((max(y, w) + x), z) < (y + x)), false) ||
 rewrite((max((max(y, z) + x), w) < (x + y)), false) ||
 rewrite((max((max(y, z) + x), w) < (y + x)), false) ||
 rewrite((max((max(z, w) + (x + y)), u) < (z + (y + x))), false) ||
 rewrite((max((max(z, w) + (x + y)), u) < ((y + x) + z)), false) ||
 rewrite((max((max(z, w) + (y + x)), u) < (z + (x + y))), false) ||
 rewrite((max((max(z, w) + (y + x)), u) < ((x + y) + z)), false) ||
 rewrite((max((max(z, x) + y), w) < (x + y)), false) ||
 rewrite((max((max(z, x) + y), w) < (y + x)), false) ||
 rewrite((max((max(z, y) + x), w) < (x + y)), false) ||
 rewrite((max((max(z, y) + x), w) < (y + x)), false) ||
 rewrite((max((max((x + y), w) + z), u) < (z + (y + x))), false) ||
 rewrite((max((max((x + y), w) + z), u) < ((y + x) + z)), false) ||
 rewrite((max((max((y + x), w) + z), u) < (z + (x + y))), false) ||
 rewrite((max((max((y + x), w) + z), u) < ((x + y) + z)), false) ||
 rewrite((max((z - y), c2) < (max((x - y), c0) + 1)), (max(y, z) <= x), ((max(c0, c2) <= 0) && ((max(c0, -1) + 1) <= c2))) ||
 rewrite((max((x*c0), c1) < (max(x, y)*c0)), (max(x, 0) < y), (((0 <= c1) && ((c1 + 1) <= (c0*2))) && ((max(c1, 0) + 1) <= c0))) ||
 rewrite((max((x*c1), c2) < (max(x, c0)*c1)), false, (((((((-1 <= c2) || (0 <= c0)) || (c0 <= c2)) || (c1 <= -1)) && (((c0 <= 0) || (c1 <= -1)) && (((c0 <= 1) || (c1 <= -1)) || ((c0*c1) <= c1)))) || ((c0*c1) <= c2)) && ((((0 <= c2) || (1 <= c0)) || (c0 <= -1)) || (c1 <= -1)))) ||
 rewrite((max(((x + c2)/c0), c3) < max((x/c0), c1)), false, ((((c0 < ((c0 + c2) + 1)) || (c1 < (c3 + 1))) || (0 <= c2)) && (((0 <= c2) || (c0 < ((c0 + c2) + 1))) && (((c1 < (c3 + 1)) && (((c1 + 1)*c0) < (((c0*c3) + ((c0*2) + c2)) + 1))) && (0 <= c0))))) ||
 rewrite((max(((x*c0)/c2), c3) < (((x*c0) + c1)/c2)), false, (((0 <= c1) || (1 <= c2)) && ((c1 <= 0) || (c2 <= -1)))) ||
 rewrite((max(min(x, y), (w + min(z, c0))) < min(x, y)), false) ||
 rewrite((max(min(x, y), (w + min(z, c0))) < min(y, x)), false) ||
 rewrite((max(min(x, y), (min(z, c0) + w)) < min(x, y)), false) ||
 rewrite((max(min(x, y), (min(z, c0) + w)) < min(y, x)), false) ||
 rewrite((max(min(x, (y + c0)), z) < min(x, (y + c0))), false) ||
 rewrite((max(min(x, (y + c0)), z) < min((y + c0), x)), false) ||
 rewrite((max(min(y, x), (w + min(z, c0))) < min(x, y)), false) ||
 rewrite((max(min(y, x), (w + min(z, c0))) < min(y, x)), false) ||
 rewrite((max(min(y, x), (min(z, c0) + w)) < min(x, y)), false) ||
 rewrite((max(min(y, x), (min(z, c0) + w)) < min(y, x)), false) ||
 rewrite((max(min((y + c0), x), z) < min(x, (y + c0))), false) ||
 rewrite((max(min((y + c0), x), z) < min((y + c0), x)), false) ||
 rewrite((max(max(x, y), z) < x), false) ||
 rewrite((max(max(y, x), z) < x), false) ||
 rewrite((select((x < (y + c1)), min((y - x), c2), c0) < c0), false, ((c0 <= c2) && ((c0 + c1) <= 1))) ||
 rewrite((select((y < z), c1, min(x, c0)) < min(x, c0)), false, (c0 <= c1)) ||
 rewrite((select((y < z), min(x, c0), c1) < min(x, c0)), false, (c0 <= c1)) ||
 rewrite((select((z < x), y, (y + c0)) < (y + min(x, c0))), false, (c0 <= 0)) ||
 rewrite((select((z < x), y, (y + c0)) < (min(x, c0) + y)), false, (c0 <= 0)) ||
 rewrite((select((min(x, c0) < (y + c1)), c2, min(x, c0)) < min(x, c0)), false, (c0 <= c2)) ||


#endif

              // Synthesized
              #if USE_SYNTHESIZED_RULES


              rewrite(((((c0 - x)/c1)*c2) < x), true, (((c0 == (c1 + 1)) && ((c1 + c2) == 0)) && ((0 < c1) && (c1 < 16)))) || // Predicate too specific
              rewrite((((c0 - x)/c1) < y), ((y*fold((0 - c1))) < x), (((0 < c1) && (c1 < 16)) && (c0 == 0))) || // < 16
              rewrite((((x + y)*z) < ((y*z) + w)), ((x*z) < w)) ||
              rewrite(((min(x, c0) + y) < min(z, y)), ((min(x, c0) + y) < z), (c0 < 0)) ||
              rewrite(((x*c0) < ((y*c0) + z)), (((x - y)*c0) < z)) || // Unnecessary constant
              rewrite(((x*c0) < ((y*c1) + c2)), ((x*fold((c0/c1))) < y), (((((c1 < 16) && (0 < c1)) && ((c1 != 0) && ((c0 % c1) == 0))) && (c2 <= 0)) && (0 < (c1 + c2)))) || // < 16. Some unnecessary constraints.
              rewrite((max(min((x + c0), y), z) < x), (max(y, z) < x), (0 <= c0)) ||
              rewrite((max(x, c0) < max(y, 0)), (x < max(y, 0)), (c0 < 0)) ||
              rewrite((min(min(x, y), z) < x), (min(y, z) < x)) ||
              rewrite((min(x, y) < min(z, (x + c0))), (min(x, y) < z), (0 < c0)) ||
              rewrite((x < ((x + y) + z)), (0 < (y + z))) ||
              rewrite((x < max((max(y, x) + c0), z)), true, (0 < c0)) ||
              rewrite((x < max(y, (max(z, x) + c0))), true, (0 < c0)) ||
              rewrite(((((((x - y) + c0)/z)*z) + y) < x), true, (c0 < 0)) ||
              rewrite(((min(((x + c0)/y), z)*y) < (x + y)), (c0 < (select((x < z), 0, c0) + y))) ||

              rewrite((((x + ((y + c0)/c1))*c1) < y), (x <= 1), ((((0 < c1) && (c1 < 16)) && ((c0 + c1) < 0)) && (-1 <= (c0 + c1)))) || // Predicate is goofy way to say c0 + c1 == -1. RHS feels like it could be more general too

              rewrite((((x + y)*c0) < (z + (y*c0))), ((x*c0) < z)) ||
              rewrite((max(x, c0) < ((max((x*c1), y) + c2)/c1)), (fold((c0 - c2)) < max((x*c1), y)), (((c0 == -1) || (c1 == 0)) && (((0 < c1) && (c1 < 16)) && (c1 <= c2)))) ||
              rewrite((max(x, y) < ((max((x*c0), c1) + c2)/c0)), (y <= max(x, fold((c0 - c2)))), (((((0 < c0) && (c0 == (c2 + -1))) && (c0 < 16)) && ((c1 + c2) < c0)) && (0 <= (c0 + c1)))) ||

              rewrite(((x + y) < max(((max(y, z) + x) + c0), w)), true, (1 <= c0)) ||

              // From Google list
              rewrite((x < (y + 1)), (x <= y)) ||
              #endif

              false))) {
            return mutate(std::move(rewrite.result), bounds);
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return LT::make(a, b);
    }
}


// The other comparison operators redirect to the less-than operator
Expr Simplify::visit(const LE *op, ExprInfo *bounds) {
    if (!may_simplify(op->a.type())) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return LE::make(a, b);
        }
    }

    Expr mutated = mutate(!(op->b < op->a), bounds);
    if (const LE *le = mutated.as<LE>()) {
        Expr a = le->a, b = le->b;

        auto rewrite = IRMatcher::rewriter(IRMatcher::le(a, b), op->type, a.type());

#if USE_SYNTHESIZED_RULES_V2
        if (no_overflow_int(a.type())) {

            if (false ||

                false) {
                return mutate(rewrite.result, bounds);
            }
        }
#endif


        // Synthesized rules
#if USE_SYNTHESIZED_RULES
        if (no_overflow_int(a.type())) {

            if (rewrite((x <= max(max(y, x), z)), true) ||
                rewrite((x <= max(max(x, y), z)), true) ||
                rewrite((x <= select((y < z), w, x)), x <= w || z <= y) ||
                rewrite(((x + y) <= min(z, (w + y))), (x <= min((z - y), w))) ||
                rewrite(((x + y) <= min(z, (y + w))), (x <= min((z - y), w))) ||
                rewrite(((x + y) <= min((y + z), w)), (x <= min((w - y), z))) ||
                rewrite(((x + y) <= min((z + y), w)), (x <= min((w - y), z))) ||
                rewrite(((x + y) <= max(z, (w + x))), (y <= max((z - x), w))) ||
                rewrite(((x + y) <= max(z, (w + y))), (x <= max((z - y), w))) ||
                rewrite(((x + y) <= max((z + y), w)), (x <= max((w - y), z))) ||
                rewrite(((x + y) <= (max(z, (w + y)) + u)), (x <= (max((z - y), w) + u))) ||
                rewrite(((x + (y*z)) <= (w*z)), (x <= ((w - y)*z))) ||
                rewrite((((x + y) + z) <= y), ((x + z) <= 0)) ||
                rewrite((min(x, y) <= min(x, z)), (min(x, y) <= z)) ||
                rewrite((min(x, y) <= min(z, y)), (min(x, y) <= z)) ||
                rewrite((min(x, (y + z)) <= (y + w)), (min((x - y), z) <= w)) ||
                rewrite((min(x, (y + z)) <= (z + w)), (min((x - z), y) <= w)) ||
                rewrite((min((x + y), z) <= (x + w)), (min((z - x), y) <= w)) ||
                rewrite((min((x + y), z) <= (y + w)), (min((z - y), x) <= w)) ||
                rewrite((min(min(x, y), z) <= x), true) ||
                rewrite((min(min(x, y), z) <= y), true) ||
                rewrite((max(x, y) <= max(z, y)), (x <= max(y, z))) ||
                rewrite((select((x < y), z, w) <= w), z <= w || y <= x) ||

                rewrite((min(x, (y + z)) <= (w + z)), (min((x - z), y) <= w)) ||
                rewrite((min((x + y), z) <= (w + y)), (min((z - y), x) <= w)) ||
                rewrite((min(min(x, y), z) <= min(w, y)), (min(min(x, y), z) <= w)) ||

                rewrite(((min(x, y) + z) <= (max(w, z) + y)), const_true()) ||

                rewrite(min(x, y) <= min(y, x), true) ||

                rewrite((x <= select((y < z), x, w)), ((x <= w) || (y < z))) ||
                rewrite((((x + y) + z) <= min((y + w), u)), ((x + z) <= min((u - y), w))) ||
                rewrite((((x + y) + z) <= max((y + w), u)), ((x + z) <= max((u - y), w))) ||
                rewrite(((((x + y) + z) + w) <= (y + u)), (((x + w) + z) <= u)) ||
                rewrite(((min(x, y) + z) <= (min(z, w) + x)), (z <= (max((x - y), 0) + w))) ||
                rewrite(((x*y) <= ((y*z) + w)), (((x - z)*y) <= w)) ||
                rewrite((min(x, y) <= min(y, z)), (min(x, y) <= z)) ||
                rewrite((min(x, y) <= min(min(x, z), w)), (min(x, y) <= min(z, w))) ||
                rewrite((min(x, y) <= max(x, y)), true) ||
                //rewrite((min((x + y), z) <= ((x + y) + w)), (min((x + y), z) <= ((x + y) + w))) ||
                rewrite((min(min(x, y), z) <= min(x, w)), (min(min(x, z), y) <= w)) ||
                rewrite((min(max(x, y), z) <= max(x, w)), (min(y, z) <= max(x, w))) ||

                rewrite((((x + (y + z)) + c0) <= (w + z)), (((x + y) + c0) <= w)) ||
                rewrite(((min((min(x, c0) + y), z) + c1) <= y), true, ((c0 + c1) == 0)) ||
                rewrite((((x + y)*c1) <= (z + (x*c1))), ((y*c1) <= z)) ||
                rewrite((((x + y)*z) <= (w + (y*z))), ((x*z) <= w)) ||
                rewrite((min(x, y) <= max(y, x)), true) ||
                rewrite((min((x*y), c0) <= (min(x, c1)*y)), true, ((0 <= c1) && (c0 <= 0))) ||
                rewrite((max(x, c0) <= max(y, 0)), (x <= max(y, 0)), (c0 <= 0)) ||
                rewrite((select((x < y), max(z, w), z) <= max(z, w)), true) ||

                rewrite((min((min(x, y)*c1), z) <= (x*c1)), true, (0 <= c1)) ||
                rewrite((min(min((min(x, c0) + y), z), w) <= y), true, (c0 <= 0)) ||
                rewrite(((min((min(x, y) + c0), z) + c1) <= y), true, ((c0 + c1) <= 0)) ||

                rewrite((x <= (max(y, c0) + min(x, z))), (x <= (max(y, c0) + z)), (0 <= c0)) ||
                rewrite(((x + c0) <= (min(x, c1) + y)), ((max(x, c1) + fold((c0 - c1))) <= y)) ||
                // c0 unbound. Shouldn't have eq constraints
                // rewrite((((max(x, 0) + y) + c1) <= x), (y < (min(x, c0) + 2)), ((c0 == 0) && (c1 == -1))) ||
                rewrite((((max(x, 0) + y) + z) <= x), ((y + z) <= min(x, 0))) ||
                rewrite(((max(x, c0) + c1) <= max(y, c2)), ((x + c1) <= max(y, c2)), (((c0 != 0) && ((c1/c0) == 0)) && ((c0 <= 0) && (0 <= c2)))) || // weird one
                rewrite(((x*c1) <= ((y*c1) + z)), (((x - y)*c1) <= z)) ||
                rewrite((min(x, y) <= min(min(y, z), c0)), (min(x, y) <= min(z, c0))) ||
                rewrite((min((x + y), z) <= (w + x)), (min((z - x), y) <= w)) ||
                rewrite((min(((x*c0) + y), z) <= y), ((z <= y) || (x < 1)), (0 < c0)) ||
                rewrite((min(((x*c0) + y), z) <= y), (select((y < z), 1, 0) <= select((0 < x), 0, 1)), (0 < c0)) || // crappy form

                rewrite(((x + (y*z)) <= ((w + z)*y)), (x <= (w*y))) ||
                rewrite(((max(x, 0) + min(y, 1)) <= y), (x < max(y, 1))) || // Too specific?

                rewrite((((x + c0)/c1) <= y), (x <= (y*c1)), (((0 < c1) && (c1 < 16)) && (c0 == (c1 + -1)))) ||
                rewrite((((c0 - x)/c3) <= ((c2 - x)/c3)), true, (((0 < c3) && (c3 < 16)) && (c0 <= c2))) ||
                rewrite(((min(x, y)/c0) <= (y/c1)), true, ((((c1 != 0) && ((c0 % c1) == 0)) && (c0 <= c1)) && ((((0 < c0) && (c0 < 16)) && (0 < c1)) && (c1 < 16)))) || // Crazy predicate. c0 % c1 == 0 && c0 <= c1 should just be c0 == c1. TODO: throw all the predicates into the system too
                rewrite((min(x, y) <= max(x, z)), true) ||
                rewrite((min(x, y) <= max(y, z)), true) ||
                rewrite((min((min(x, c0) + y), z) <= y), true, (c0 <= 0)) ||
                rewrite((min((x*y), c0) <= (min(x, c1)*y)), true, ((c0 <= c1) && (0 <= c0))) ||

                rewrite((x <= ((y + c0)/c1)), ((x*c1) < y), (((-1 <= c0) && (c0 < 0)) && ((0 < c1) && (c1 < 16)))) || // Predicate implies c0 == 1
                rewrite(((x*c1) <= max((y*c1), c2)), (x <= max(y, fold((c2/c1)))), ((c1 < 16) && (0 < c1))) || // < 16
                rewrite((min((x*c2), c1) <= (y*c2)), (min(x, fold((((c1 + -1)/c2) + 1))) <= y), ((c2 < 16) && (0 < c2))) || // < 16

                rewrite(((min(x, c0) + c1) <= min(y, 0)), (min(x, c0) < (y + fold((c0 - c1)))), ((c1 < 0) && (c0 == 1))) || // Should have subs'd in c0
                rewrite(((max(x, c0) + c1) <= max(y, c0)), ((x + c1) <= max(y, c0)), (c1 <= 0)) ||
                rewrite((x <= (min(((x + c0)/c1), y)*c2)), (x <= (y*c1)), ((((c0 == (c1 + -1)) && (c0 == (c2 + -1))) && (0 < c1)) && (c1 < 16))) || // < 16, predicate implies c1 == c2 indirectly (super simplify the predicate!)
                rewrite((x <= (y + ((((x - y) + c0)/c1)*c1))), true, (((0 < c1) && (c0 == (c1 + -1))) && (c1 < 16))) ||
                rewrite((x <= (((((x - y) + c0)/c1)*c1) + y)), true, (((0 < c1) && (c0 == (c1 + -1))) && (c1 < 16))) ||
                rewrite(((((((x - y)/z)*z) + y) + c0) <= x), true, (c0 <= 0)) ||
                rewrite((((x*c0) + c1) <= min((y*c0), c2)), ((x + c2) <= min(y, 0)), (((c2 == -1) || (c0 == 0)) && ((c1 < (c2 + -1)) && (c2 == (c0 + c1))))) || // c0 can't be zero, but subtrees are already simplified. With that removed, rest of predicate becomes fishy
                rewrite((((x*c0) + y) <= (((x*c1) + z)*c2)), (y <= (z*c2)), (c0 == (c1*c2))) ||
                rewrite(((((x + y)*c0) + z) <= ((y*c0) + w)), (((x*c0) + z) <= w)) ||
                rewrite(((((((x - y) + c0)/z)*z) + y) <= x), true, (c0 <= 0)) ||
                rewrite(((min((x + c0), y) + c1) <= max((x + c2), z)), true, ((c0 + c1) <= c2)) ||
                rewrite(((x*c0) <= (min((x*c0), c1) + (y*c0))), (max(x, fold(c1/c0)) < y), (((((0 < c0) && (c0 < 16)) && (c1 < 0)) && (0 <= (c0 + c1))))) || // Fishy last clause
                rewrite((((x + y)/c0) <= ((max(x, z) + y)/c0)), true, ((0 < c0) && (c0 < 16))) ||
                rewrite((((min(x, y) + z)/c0) <= ((x + z)/c0)), true, ((0 < c0) && (c0 < 16))) ||
                rewrite((((min(x, y) + z)/c0) <= ((y + z)/c0)), true, ((0 < c0) && (c0 < 16))) ||
                rewrite((min(((x + y) + z), w) <= ((x + z) + y)), true) ||
                rewrite((min(((x + y) + z), w) <= ((z + x) + y)), true) ||
                // rewrite((min((((x*c0) + y) + c1), z) <= y), (select((y < z), 1, fold((c0 + c1))) <= select((x < 2), 1, fold((c0 + c1)))), ((0 < c0) && ((c0 + c1) == 0))) || // Not sure what to make of this one
                rewrite((min((((x*c0) + y) + c1), z) <= y), (z <= y) || (x < 2), ((0 < c0) && ((c0 + c1) == 0))) || // Hand-simplified form of commented-out rule
                rewrite((min(min((min(x, y) + z), w), u) <= (y + z)), true) ||

                rewrite((x <= (min(x, c0) + (((max(x, c0) + c1)/c0)*c0))), true, (((0 < c0) && (c0 < 16)) && (-1 <= c1))) ||
                rewrite(((x + c0) <= (((((x - y) + c1)/c2)*c2) + y)), true, (((0 < c2) && (c2 < 16)) && (((c0 + c2) + -1) <= c1))) ||
                rewrite(((min(x, y) + c0) <= min(x, c1)), (min(x, y) <= fold((c1 - c0))), (c0 <= 0)) ||
                rewrite(((max(x, c0) + c1) <= max(y, c2)), ((x + c1) <= max(y, c2)), ((0 <= c2) && ((c0 + c1) <= 0))) ||
                rewrite(((x + c0) <= ((((x - y)/c1)*c1) + y)), true, (((0 < c1) && (c1 < 16)) && ((c0 + c1) <= 1))) ||
                rewrite(((((x + y) + z) + w) <= x), (((y + z) + w) < 1)) ||
                rewrite(((((x + y) + z) + w) <= y), (((x + z) + w) < 1)) ||

                rewrite((x <= (min(x, c0) + (((max(x, c0) + c1)/c2)*c2))), true, ((c2 + -1) <= (c0 + c1))) ||
                rewrite(((x + c0) <= (y + (((x - y)/c1)*c1))), true, (((0 < c1) && (c1 < 16)) && ((c0 + c1) <= 1))) ||
                rewrite((((x*c0) + c1) <= min((y*c0), c1)), (x <= min(y, fold((((c1 + -1)/c0) + 1)))), (((0 < (min(c1, 0) + c0)) && (c0 < 16)) && (c1 <= 0))) ||
                rewrite(((((x/c0)*c0) + c1) <= ((x/c2)*c2)), true, ((c1 + c2) < 2)) ||

                rewrite((((x + (y*c0)) + c1) <= ((z + y)*c0)), ((x + c1) <= (z*c0))) ||
                rewrite(((((x/c0)*c0) + c1) <= ((x/c2)*c2)), true, ((c1 + c2) < 2)) ||
                rewrite(((min((x + c0), y) + c1) <= max(z, (x + c2))), true, ((c0 + c1) <= c2)) ||
                rewrite(((x*c0) <= (y + ((z + x)*c0))), ((z*fold((0 - c0))) <= y)) ||
                rewrite((((x + y)/c0) <= ((max(z, x) + y)/c0)), true, ((0 < c0) && (c0 < 16))) ||
                rewrite((min(x, y) <= (((y + c0)/c1)*c1)), true, (((0 < c1) && (c0 == (c1 + -1))) && (c1 < 16))) ||
                rewrite((min(x, ((y + z) + w)) <= ((y + w) + z)), true) ||

                // From google list
                rewrite((min(x, y) <= max(z, y)), true) ||
                rewrite((max(x, y) <= max(x, z)), (y <= max(x, z))) ||
                rewrite((min(x, y) <= min(z, x)), (min(x, y) <= z)) ||
                rewrite(((min(x, y) + z) <= max(w, (z + y))), true) ||
                rewrite((min(max(x, y), z) <= max(min(y, z), w)), (min(x, z) <= max(w, y))) ||

                rewrite(((x + (y*c0)) <= ((z + (y*c1))*c2)), (x <= (z*c2)), (c0 == (c1*c2))) ||
                rewrite(((x + ((y + c0)/c1)) <= ((y + c2)/c1)), (x <= 1), (((0 < c1) && (c2 == (c0 + c1))) && (c1 < 16))) ||

                rewrite(((x + 1) <= y), (x < y)) ||

                // implicit rewrite(((x + ((y + c0)/c1)) <= ((y + c2)/c1)), (x < c3), (((0 <= c1) && ((c1 + c2) <= ((c1*c3) + c0))) && (((c1*c3) + (c0 + c1)) <= ((c1*2) + c2)))) ||
                rewrite(((min((min(x, y) + c0), z) + c1) <= x), true, ((c0 + c1) <= 0)) ||
                rewrite(((min(min(min((x + c0), y), z), w) + c1) <= x), true, ((c0 + c1) <= 0)) ||

                false) {
                return mutate(rewrite.result, bounds);
            }
        }
#endif

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        }
    }
    return mutated;
}

Expr Simplify::visit(const GT *op, ExprInfo *bounds) {
    if (!may_simplify(op->a.type())) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return GT::make(a, b);
        }
    }

    return mutate(op->b < op->a, bounds);
}

Expr Simplify::visit(const GE *op, ExprInfo *bounds) {
    if (!may_simplify(op->a.type())) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return GE::make(a, b);
        }
    }

    return mutate(!(op->a < op->b), bounds);
}

}
}
