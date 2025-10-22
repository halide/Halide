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

    ExprInfo a_info, b_info;
    Expr a = mutate(op->a, &a_info);
    Expr b = mutate(op->b, &b_info);

    if (should_commute(a, b)) {
        std::swap(a, b);
        std::swap(a_info, b_info);
    }

    if (a.type().is_bool()) {
        auto rewrite = IRMatcher::rewriter(IRMatcher::eq(a, b), op->type);
        if (rewrite(x == x, true) ||
            rewrite(x == neg(x), false) ||
            rewrite(x == 1, x) ||
            rewrite(x == 0, !x)) {
            return mutate(rewrite.result, info);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(a, b);
        }
    }

    // Attempt to prove or disprove using bounds analysis
    ExprInfo delta_info;
    delta_info.bounds = a_info.bounds - b_info.bounds;
    delta_info.alignment = a_info.alignment - b_info.alignment;

    // debug(0) << delta_info.bounds << " " << delta_info.alignment << "\n";

    if (delta_info.bounds.is_single_point(0)) {
        return const_true(op->type.lanes(), info);
    } else if (!delta_info.bounds.contains(0) ||
               delta_info.alignment.remainder != 0) {
        return const_false(op->type.lanes(), info);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::eq(a, b), op->type, a.type());

    // clang-format off
    if (EVAL_IN_LAMBDA
        (rewrite(x == x, true) ||
         rewrite(c0 == c1, fold(c0 == c1)) ||
         rewrite(min(x, y) == min(y, x), true) ||
         rewrite(max(x, y) == max(y, x), true) ||
         rewrite(x + y == y + x, true) ||
         rewrite(x * y == y * x, true) ||

         rewrite(x + c0 == c1, x == fold(c1 - c0)) ||
         rewrite(x + c0 == y + c1, x == y + fold(c1 - c0)) ||

         // Turn subtracts into additions on the other side
         rewrite(x - y == z, x == y + z) ||
         rewrite(x == y - z, x + z == y) ||

         rewrite((x + (y - z)) == w, x + y == w + z) ||
         rewrite(((y - z) + x) == w, x + y == w + z) ||
         rewrite(w == (x + (y - z)), w + z == x + y) ||
         rewrite(w == ((y - z) + x), w + z == x + y) ||

         // Subtract a term from both sides
         rewrite((x + y) == x, y == 0) ||
         rewrite((x + y) == y, x == 0) ||
         rewrite(x == (x + y), y == 0) ||
         rewrite(y == (x + y), x == 0) ||

         rewrite((x + y) == (x + z), y == z) ||
         rewrite((x + y) == (z + x), y == z) ||
         rewrite((y + x) == (x + z), y == z) ||
         rewrite((y + x) == (z + x), y == z) ||

         rewrite(((x + y) + z) == x, y + z == 0) ||
         rewrite(((y + x) + z) == x, y + z == 0) ||
         rewrite((z + (x + y)) == x, z + y == 0) ||
         rewrite((z + (y + x)) == x, z + y == 0) ||
         rewrite(x == ((x + y) + z), y + z == 0) ||
         rewrite(x == ((y + x) + z), y + z == 0) ||
         rewrite(x == (z + (x + y)), z + y == 0) ||
         rewrite(x == (z + (y + x)), z + y == 0) ||

         rewrite((x + y) == (z + (w + x)), y == (z + w)) ||
         rewrite((x + y) == (z + (w + y)), x == (z + w)) ||
         rewrite((x + y) == (z + (x + w)), y == (z + w)) ||
         rewrite((x + y) == (z + (y + w)), x == (z + w)) ||
         rewrite((x + y) == ((x + z) + w), y == (z + w)) ||
         rewrite((x + y) == ((y + z) + w), x == (z + w)) ||
         rewrite((x + y) == ((z + x) + w), y == (z + w)) ||
         rewrite((x + y) == ((z + y) + w), x == (z + w)) ||

         // ... These cancellation rules could go arbitrarily deep. We stop at the
         // same point as the sub visitor.

         rewrite(max(x, y) == y, x <= y) ||
         rewrite(min(x, y) == y, y <= x) ||
         rewrite(max(y, x) == y, x <= y) ||
         rewrite(min(y, x) == y, y <= x) ||
         rewrite(y == max(x, y), x <= y) ||
         rewrite(y == min(x, y), y <= x) ||
         rewrite(y == max(y, x), x <= y) ||
         rewrite(y == min(y, x), y <= x) ||

         rewrite(max(x, c0) == c1, false, c1 < c0) ||
         rewrite(max(x, c0) == c1, x == c1, c0 < c1) ||
         rewrite(min(x, c0) == c1, false, c0 < c1) ||
         rewrite(min(x, c0) == c1, x == c1, c1 < c0) ||

         rewrite(ramp(x, y, c0) == ramp(z, c1, c0), ramp(x, y + fold(-c1), c0) == broadcast(z, c0)) ||

         rewrite(ramp(x, y, c0) == broadcast(z, c0),
                 ramp(x - z, y, c0) == broadcast(0, c0), !is_const(z)) ||

         rewrite(ramp(x, y, c0) == broadcast(z, c1),
                 ramp(x - broadcast(z, fold(c1 / c0)), y, c0) == broadcast(0, c0),
                 !is_const(z) && (c1 % c0 == 0)) ||

         rewrite(broadcast(x, c0) == broadcast(y, c0), broadcast(x == y, c0)) ||

         rewrite(broadcast(x, c0) == broadcast(y, c1),
                 broadcast(x == broadcast(y, fold(c1 / c0)), c0), c1 % c0 == 0) ||

         rewrite(broadcast(y, c1) == broadcast(x, c0),
                 broadcast(broadcast(y, fold(c1 / c0)) == x, c0), c1 % c0 == 0) ||

         rewrite((x - broadcast(y, c0)) == broadcast(z, c0), x == broadcast(z + y, c0)) ||
         rewrite((x + broadcast(y, c0)) == broadcast(z, c0), x == broadcast(z - y, c0)) ||

         rewrite(select(x, y, z) == y, x || (z == y)) ||
         rewrite(select(x, y, z) == z, !x || (y == z)) ||
         rewrite(y == select(x, y, z), x || (y == z)) ||
         rewrite(z == select(x, y, z), !x || (z == y)) ||
         rewrite(select(x, c0, y) == c1, !x && (y == c1), c0 != c1) ||
         rewrite(select(x, y, c0) == c1, x && (y == c1), c0 != c1) ||
         rewrite(select(x, y, z) == select(x, w, u), select(x, y == w, z == u)) ||

         rewrite(select(x, y + w, z) == y, select(x, w == 0, z == y)) ||
         rewrite(select(x, w + y, z) == y, select(x, w == 0, z == y)) ||
         rewrite(select(x, y, z + w) == z, select(x, y == z, w == 0)) ||
         rewrite(select(x, y, w + z) == z, select(x, y == z, w == 0)) ||
         rewrite(y == select(x, y + w, z), select(x, w == 0, z == y)) ||
         rewrite(y == select(x, w + y, z), select(x, w == 0, z == y)) ||
         rewrite(z == select(x, y, z + w), select(x, y == z, w == 0)) ||
         rewrite(z == select(x, y, w + z), select(x, y == z, w == 0)) ||

         rewrite(select(x, y + (z + w), u) == w, select(x, y + z == 0, u == w)) ||
         rewrite(select(x, y + (z + w), u) == z, select(x, y + w == 0, u == z)) ||
         rewrite(select(x, (y + z) + w, u) == y, select(x, w + z == 0, u == y)) ||
         rewrite(select(x, (y + z) + w, u) == z, select(x, w + y == 0, u == z)) ||
         rewrite(w == select(x, y + (z + w), u), select(x, y + z == 0, u == w)) ||
         rewrite(z == select(x, y + (z + w), u), select(x, y + w == 0, u == z)) ||
         rewrite(y == select(x, (y + z) + w, u), select(x, w + z == 0, u == y)) ||
         rewrite(z == select(x, (y + z) + w, u), select(x, w + y == 0, u == z)) ||

         rewrite(select(x, y + z, w) == (u + y), select(x, z, w - y) == u) ||
         rewrite(select(x, z + y, w) == (u + y), select(x, z, w - y) == u) ||
         rewrite(select(x, y + z, w) == (y + u), select(x, z, w - y) == u) ||
         rewrite(select(x, z + y, w) == (y + u), select(x, z, w - y) == u) ||

         rewrite(select(x, y, z) == select(x, u, v) + w, select(x, y - u, z - v) == w) ||
         rewrite(select(x, y, z) == w + select(x, u, v), select(x, y - u, z - v) == w) ||

         rewrite(x * y == z * y, (x - z) * y == 0) ||
         rewrite(x * y == y * z, (x - z) * y == 0) ||
         rewrite(y * x == z * y, (x - z) * y == 0) ||
         rewrite(y * x == y * z, (x - z) * y == 0) ||

         rewrite(z * y == (x * y + u), (z - x) * y == u) ||
         rewrite(y * z == (x * y + u), (z - x) * y == u) ||
         rewrite(z * y == (y * x + u), (z - x) * y == u) ||
         rewrite(y * z == (y * x + u), (z - x) * y == u) ||

         rewrite(slice(x, c0, c1, c2) == slice(y, c0, c1, c2),
                 slice(x == y, c0, c1, c2), c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite(slice(x, c0, c1, c2) == z + slice(y, c0, c1, c2),
                 slice(x - y, c0, c1, c2) == z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         rewrite(slice(x, c0, c1, c2) == slice(y, c0, c1, c2) + z,
                 slice(x - y, c0, c1, c2) == z, c2 > 1 && lanes_of(x) == lanes_of(y)) ||
         false) ||
        (no_overflow(a.type()) && EVAL_IN_LAMBDA
         (rewrite(x * y == 0, (x == 0) || (y == 0)) ||
          rewrite(x * y == x, (x == 0) || (y == 1)) ||
          rewrite(x == x * y, (x == 0) || (y == 1)) ||
          rewrite(y * x == x, (x == 0) || (y == 1)) ||
          rewrite(x == y * x, (x == 0) || (y == 1)) ||
          rewrite(x * c0 == c1, x == fold(c1 / c0), c1 % c0 == 0) ||
          rewrite(x * c0 == c1, false, c1 % c0 != 0) ||

          rewrite(x == min(x - y, 0), max(x, y) == 0) ||
          rewrite(x == max(x - y, 0), min(x, y) == 0) ||
          rewrite(min(x - y, 0) == x, max(x, y) == 0) ||
          rewrite(max(x - y, 0) == x, min(x, y) == 0) ||
          rewrite(min(x, y) == x + y, max(x, y) == 0) ||
          rewrite(min(y, x) == x + y, max(y, x) == 0) ||
          rewrite(max(x, y) == x + y, min(x, y) == 0) ||
          rewrite(max(y, x) == x + y, min(y, x) == 0) ||

          rewrite(min(z, x + y) == x, min(z - x, y) == 0) ||
          rewrite(min(x + y, z) == x, min(y, z - x) == 0) ||
          rewrite(min(z, y + x) == x, min(z - x, y) == 0, !is_const(x)) ||
          rewrite(min(y + x, z) == x, min(y, z - x) == 0, !is_const(x)) ||
          rewrite(x == min(z, x + y), min(z - x, y) == 0) ||
          rewrite(x == min(x + y, z), min(y, z - x) == 0) ||
          rewrite(x == min(z, y + x), min(z - x, y) == 0) ||
          rewrite(x == min(y + x, z), min(y, z - x) == 0) ||

          rewrite(max(z, x + y) == x, max(z - x, y) == 0) ||
          rewrite(max(x + y, z) == x, max(y, z - x) == 0) ||
          rewrite(max(z, y + x) == x, max(z - x, y) == 0, !is_const(x)) ||
          rewrite(max(y + x, z) == x, max(y, z - x) == 0, !is_const(x)) ||
          rewrite(x == max(z, x + y), max(z - x, y) == 0) ||
          rewrite(x == max(x + y, z), max(y, z - x) == 0) ||
          rewrite(x == max(z, y + x), max(z - x, y) == 0) ||
          rewrite(x == max(y + x, z), max(y, z - x) == 0) ||

          // Consider min(x, y) == min(z, w), where you know x < z by some other means
          // It can be broken into four cases:

          // 1) x == z && x <= y && z <= w
          // We have x == z, which contradicts x < z, so we can drop this case.

          // 2) x == w && x <= y && w <= z
          // We already know x < z, so given that w == x, w <= z is
          // redundant. This case therefore simplifies to:
          // x == w && x <= y

          // 3) y == z && y <= x && z <= w
          // y == z && y <= x, implies z <= x, which contradicts x < z, so we
          // can drop this case.

          // 4) y == w && y <= x && w <= z
          // y <= x implies y < z. Given y == w, w < z. So we can drop
          // the last clause and this case simplifies to:
          // y == w && y <= x

          // Putting it back together, we have:
          //   (x <= y && x == w) || (y <= x && y == w)
          // = min(x, y) == w

          // So in min(x, y) == min(z, w), if we know a term on one side is strictly larger than a term on the other side, we can drop it.

          rewrite(min(x, c0) == min(y, c1), x == min(y, c1), c0 > c1) ||
          rewrite(min(x, c0) == min(y, c1), min(x, c0) == y, c0 < c1) ||

          rewrite(min(x + c0, y) == min(x, z), y == min(x, z), c0 > 0) ||
          rewrite(min(y, x + c0) == min(x, z), y == min(x, z), c0 > 0) ||
          rewrite(min(x + c0, y) == min(z, x), y == min(z, x), c0 > 0) ||
          rewrite(min(y, x + c0) == min(z, x), y == min(z, x), c0 > 0) ||

          rewrite(min(x, z) == min(x + c0, y), min(x, z) == y, c0 > 0) ||
          rewrite(min(x, z) == min(y, x + c0), min(x, z) == y, c0 > 0) ||
          rewrite(min(z, x) == min(x + c0, y), min(z, x) == y, c0 > 0) ||
          rewrite(min(z, x) == min(y, x + c0), min(z, x) == y, c0 > 0) ||

          rewrite(min(x + c0, y) == min(x, z), min(x + c0, y) == z, c0 < 0) ||
          rewrite(min(y, x + c0) == min(x, z), min(y, x + c0) == z, c0 < 0) ||
          rewrite(min(x + c0, y) == min(z, x), min(x + c0, y) == z, c0 < 0) ||
          rewrite(min(y, x + c0) == min(z, x), min(y, x + c0) == z, c0 < 0) ||

          rewrite(min(x, z) == min(x + c0, y), z == min(x + c0, y), c0 < 0) ||
          rewrite(min(x, z) == min(y, x + c0), z == min(y, x + c0), c0 < 0) ||
          rewrite(min(z, x) == min(x + c0, y), z == min(x + c0, y), c0 < 0) ||
          rewrite(min(z, x) == min(y, x + c0), z == min(y, x + c0), c0 < 0) ||

          // A similar argument applies for max
          rewrite(max(x, c0) == max(y, c1), x == max(y, c1), c0 < c1) ||
          rewrite(max(x, c0) == max(y, c1), max(x, c0) == y, c0 > c1) ||

          rewrite(max(x + c0, y) == max(x, z), y == max(x, z), c0 < 0) ||
          rewrite(max(y, x + c0) == max(x, z), y == max(x, z), c0 < 0) ||
          rewrite(max(x + c0, y) == max(z, x), y == max(z, x), c0 < 0) ||
          rewrite(max(y, x + c0) == max(z, x), y == max(z, x), c0 < 0) ||

          rewrite(max(x, z) == max(x + c0, y), max(x, z) == y, c0 < 0) ||
          rewrite(max(x, z) == max(y, x + c0), max(x, z) == y, c0 < 0) ||
          rewrite(max(z, x) == max(x + c0, y), max(z, x) == y, c0 < 0) ||
          rewrite(max(z, x) == max(y, x + c0), max(z, x) == y, c0 < 0) ||

          rewrite(max(x + c0, y) == max(x, z), max(x + c0, y) == z, c0 > 0) ||
          rewrite(max(y, x + c0) == max(x, z), max(y, x + c0) == z, c0 > 0) ||
          rewrite(max(x + c0, y) == max(z, x), max(x + c0, y) == z, c0 > 0) ||
          rewrite(max(y, x + c0) == max(z, x), max(y, x + c0) == z, c0 > 0) ||

          rewrite(max(x, z) == max(x + c0, y), z == max(x + c0, y), c0 > 0) ||
          rewrite(max(x, z) == max(y, x + c0), z == max(y, x + c0), c0 > 0) ||
          rewrite(max(z, x) == max(x + c0, y), z == max(x + c0, y), c0 > 0) ||
          rewrite(max(z, x) == max(y, x + c0), z == max(y, x + c0), c0 > 0) ||
          false)) ||

        (no_overflow_int(a.type()) &&
         (rewrite(x == x % c0, x / c0 == 0, c0 != 0) ||
          rewrite(x % c0 == x, x / c0 == 0, c0 != 0) ||
          rewrite(x == (x / c0) * c0, x % c0 == 0, c0 != 0) ||
          rewrite((x / c0) * c0 == x, x % c0 == 0, c0 != 0) ||
          rewrite(x * c0 == y * c1, x * fold(c0 / c1) == y, c0 % c1 == 0) ||
          rewrite(x * c0 == y * c1, x == y * fold(c1 / c0), c1 % c0 == 0) ||

          false)) ||

        false) {
        return mutate(rewrite.result, info);
    }
    // clang-format on

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return EQ::make(a, b);
    }
}

// NE redirects to not EQ, then the Not mutator flips any comparison op as appropriate
Expr Simplify::visit(const NE *op, ExprInfo *info) {
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
