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

        // clang-format off
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
        // clang-format on

        // clang-format off
        if (rewrite(broadcast(x) < broadcast(y), broadcast(x < y, lanes)) ||
            (no_overflow(ty) && EVAL_IN_LAMBDA
             (rewrite(ramp(x, y) < ramp(z, y), broadcast(x < z, lanes)) ||
              // Move constants to the RHS
              rewrite(x + c0 < y, x < y + fold(-c0)) ||

              // Merge RHS constant additions with a constant LHS
              rewrite(c0 < x + c1, fold(c0 - c1) < x) ||

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
              rewrite(min(x, c0) < min(x, c1) + c2, false, c0 >= c1 + c2 && c2 <= 0) ||
              rewrite(max(x, c0) < max(x, c1), false, c0 >= c1) ||
              rewrite(max(x, c0) < max(x, c1) + c2, false, c0 >= c1 + c2 && c2 <= 0) ||

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
                      c1 * (lanes - 1) >= 0)))) {
            return mutate(std::move(rewrite.result), bounds);
        }
        // clang-format on
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
        if (le->a.same_as(op->a) && le->b.same_as(op->b)) {
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

}  // namespace Internal
}  // namespace Halide
