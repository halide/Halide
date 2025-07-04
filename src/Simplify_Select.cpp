#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Select *op, ExprInfo *info) {

    ExprInfo t_info, f_info;
    Expr condition = mutate(op->condition, nullptr);
    Expr true_value = mutate(op->true_value, &t_info);
    Expr false_value = mutate(op->false_value, &f_info);

    if (info) {
        info->bounds = ConstantInterval::make_union(t_info.bounds, f_info.bounds);
        info->alignment = ModulusRemainder::unify(t_info.alignment, f_info.alignment);
        info->trim_bounds_using_alignment();
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::select(condition, true_value, false_value), op->type);

    // clang-format off
    if (EVAL_IN_LAMBDA
        (rewrite(select(IRMatcher::likely(true), x, y), true_value) ||
         rewrite(select(IRMatcher::likely(false), x, y), false_value) ||
         rewrite(select(IRMatcher::likely_if_innermost(true), x, y), true_value) ||
         rewrite(select(IRMatcher::likely_if_innermost(false), x, y), false_value) ||
         rewrite(select(1, x, y), true_value) ||
         rewrite(select(0, x, y), false_value) ||
         rewrite(select(x, y, y), false_value) ||
         rewrite(select(x, likely(y), y), false_value) ||
         rewrite(select(x, y, likely(y)), true_value) ||
         rewrite(select(x, likely_if_innermost(y), y), false_value) ||
         rewrite(select(x, y, likely_if_innermost(y)), true_value) ||
         false)) {
        if (info) {
            if (rewrite.result.same_as(true_value)) {
                *info = t_info;
            } else if (rewrite.result.same_as(false_value)) {
                *info = f_info;
            }
        }
        return rewrite.result;
    }
    // clang-format on

    // clang-format off
    if (EVAL_IN_LAMBDA
        (rewrite(select(x != y, z, w), select(x == y, w, z)) ||
         rewrite(select(x <= y, z, w), select(y < x, w, z)) ||
         rewrite(select(x, select(y, z, w), z), select(x && !y, w, z)) ||
         rewrite(select(x, select(y, z, w), w), select(x && y, z, w)) ||
         rewrite(select(x, y, select(z, y, w)), select(x || z, y, w)) ||
         rewrite(select(x, y, select(z, w, y)), select(x || !z, y, w)) ||
         rewrite(select(x, select(x, y, z), w), select(x, y, w)) ||
         rewrite(select(x, y, select(x, z, w)), select(x, y, w)) ||
         rewrite(select(x, y + z, y + w), y + select(x, z, w)) ||
         rewrite(select(x, y + z, w + y), y + select(x, z, w)) ||
         rewrite(select(x, z + y, y + w), y + select(x, z, w)) ||
         rewrite(select(x, z + y, w + y), select(x, z, w) + y) ||
         rewrite(select(x, y - z, y - w), y - select(x, z, w)) ||
         rewrite(select(x, y - z, y + w), y + select(x, -z, w)) ||
         rewrite(select(x, y + z, y - w), y + select(x, z, -w)) ||
         rewrite(select(x, y - z, w + y), y + select(x, -z, w)) ||
         rewrite(select(x, z + y, y - w), y + select(x, z, -w)) ||
         rewrite(select(x, z - y, w - y), select(x, z, w) - y) ||
         rewrite(select(x, y * z, y * w), y * select(x, z, w)) ||
         rewrite(select(x, y * z, w * y), y * select(x, z, w)) ||
         rewrite(select(x, z * y, y * w), y * select(x, z, w)) ||
         rewrite(select(x, z * y, w * y), select(x, z, w) * y) ||
         rewrite(select(x, 0 - y * z, y * w), y * select(x, 0 - z, w)) ||
         rewrite(select(x, 0 - y * z, w * y), y * select(x, 0 - z, w)) ||
         rewrite(select(x, 0 - z * y, y * w), y * select(x, 0 - z, w)) ||
         rewrite(select(x, 0 - z * y, w * y), select(x, 0 - z, w) * y) ||
         rewrite(select(x, y * z, 0 - y * w), y * select(x, z, 0 - w)) ||
         rewrite(select(x, y * z, 0 - w * y), y * select(x, z, 0 - w)) ||
         rewrite(select(x, z * y, 0 - y * w), y * select(x, z, 0 - w)) ||
         rewrite(select(x, z * y, 0 - w * y), select(x, z, 0 - w) * y) ||

         rewrite(select(x, z / y, w / y), select(x, z, w) / y) ||
         rewrite(select(x, z % y, w % y), select(x, z, w) % y) ||

         rewrite(select(x, (y + z) + u, y + w), y + select(x, z + u, w)) ||
         rewrite(select(x, (y + z) - u, y + w), y + select(x, z - u, w)) ||
         rewrite(select(x, u + (y + z), y + w), y + select(x, u + z, w)) ||
         rewrite(select(x, y + z, (y + w) + u), y + select(x, z, w + u)) ||
         rewrite(select(x, y + z, (y + w) - u), y + select(x, z, w - u)) ||
         rewrite(select(x, y + z, u + (y + w)), y + select(x, z, u + w)) ||

         rewrite(select(x, (y + z) + u, w + y), y + select(x, z + u, w)) ||
         rewrite(select(x, (y + z) - u, w + y), y + select(x, z - u, w)) ||
         rewrite(select(x, u + (y + z), w + y), y + select(x, u + z, w)) ||
         rewrite(select(x, y + z, (w + y) + u), y + select(x, z, w + u)) ||
         rewrite(select(x, y + z, (w + y) - u), y + select(x, z, w - u)) ||
         rewrite(select(x, y + z, u + (w + y)), y + select(x, z, u + w)) ||

         rewrite(select(x, (z + y) + u, y + w), y + select(x, z + u, w)) ||
         rewrite(select(x, (z + y) - u, y + w), y + select(x, z - u, w)) ||
         rewrite(select(x, u + (z + y), y + w), y + select(x, u + z, w)) ||
         rewrite(select(x, z + y, (y + w) + u), y + select(x, z, w + u)) ||
         rewrite(select(x, z + y, (y + w) - u), y + select(x, z, w - u)) ||
         rewrite(select(x, z + y, u + (y + w)), y + select(x, z, u + w)) ||

         rewrite(select(x, (z + y) + u, w + y), select(x, z + u, w) + y) ||
         rewrite(select(x, (z + y) - u, w + y), select(x, z - u, w) + y) ||
         rewrite(select(x, u + (z + y), w + y), select(x, u + z, w) + y) ||
         rewrite(select(x, z + y, (w + y) + u), select(x, z, w + u) + y) ||
         rewrite(select(x, z + y, (w + y) - u), select(x, z, w - u) + y) ||
         rewrite(select(x, z + y, u + (w + y)), select(x, z, u + w) + y) ||
         rewrite(select(x, y + (z - w), u - w), select(x, y + z, u) - w) ||
         rewrite(select(x, (y - z) + w, u - z), select(x, w + y, u) - z) ||

         rewrite(select(x, (y + z) + u, y), y + select(x, z + u, 0)) ||
         rewrite(select(x, (z + y) + u, y), y + select(x, z + u, 0)) ||
         rewrite(select(x, (y + z) - u, y), y + select(x, z - u, 0)) ||
         rewrite(select(x, (z + y) - u, y), y + select(x, z - u, 0)) ||
         rewrite(select(x, u + (y + z), y), y + select(x, u + z, 0)) ||
         rewrite(select(x, u + (z + y), y), y + select(x, u + z, 0)) ||

         rewrite(select(x, y, (y + z) + u), y + select(x, 0, z + u)) ||
         rewrite(select(x, y, (z + y) + u), y + select(x, 0, z + u)) ||
         rewrite(select(x, y, (y + z) - u), y + select(x, 0, z - u)) ||
         rewrite(select(x, y, (z + y) - u), y + select(x, 0, z - u)) ||
         rewrite(select(x, y, u + (y + z)), y + select(x, 0, u + z)) ||
         rewrite(select(x, y, u + (z + y)), y + select(x, 0, u + z)) ||

         rewrite(select(x, (y + z) + w, (u + y) + v), y + select(x, z + w, u + v)) ||
         rewrite(select(x, (y - z) + w, (u + y) + v), y + select(x, w - z, u + v)) ||

         rewrite(select(x, y + (z + w), u + (v + w)), w + select(x, z + y, u + v)) ||
         rewrite(select(x, y + (z + w), u + (v + z)), z + select(x, y + w, u + v)) ||
         rewrite(select(x, y + (z + w), u + (w + v)), w + select(x, z + y, u + v)) ||
         rewrite(select(x, y + (z + w), u + (z + v)), z + select(x, y + w, u + v)) ||
         rewrite(select(x, y + (z + w), (u + w) + v), w + select(x, z + y, u + v)) ||
         rewrite(select(x, y + (z + w), (u + z) + v), z + select(x, y + w, u + v)) ||
         rewrite(select(x, y + (z + w), (w + u) + v), w + select(x, z + y, v + u)) ||
         rewrite(select(x, y + (z + w), (z + u) + v), z + select(x, y + w, v + u)) ||
         rewrite(select(x, (y + z) + w, u + (v + y)), y + select(x, z + w, u + v)) ||
         rewrite(select(x, (y + z) + w, u + (v + z)), z + select(x, y + w, u + v)) ||
         rewrite(select(x, (y + z) + w, u + (y + v)), y + select(x, z + w, u + v)) ||
         rewrite(select(x, (y + z) + w, u + (z + v)), z + select(x, y + w, u + v)) ||
         rewrite(select(x, (y + z) + w, (u + y) + v), y + select(x, z + w, u + v)) ||
         rewrite(select(x, (y + z) + w, (u + z) + v), z + select(x, y + w, u + v)) ||
         rewrite(select(x, (y + z) + w, (y + u) + v), y + select(x, z + w, v + u)) ||
         rewrite(select(x, (y + z) + w, (z + u) + v), z + select(x, y + w, v + u)) ||

         rewrite(select(x, select(y, z, w), select(y, u, w)), select(y, select(x, z, u), w)) ||
         rewrite(select(x, select(y, z, w), select(y, z, u)), select(y, z, select(x, w, u))) ||

         rewrite(select(x < y, x, y), min(x, y)) ||
         rewrite(select(x < y, y, x), max(x, y)) ||
         rewrite(select(x < 0, x * y, 0), min(x, 0) * y) ||
         rewrite(select(x < 0, 0, x * y), max(x, 0) * y) ||

         rewrite(select(x, min(y, w), min(z, w)), min(select(x, y, z), w)) ||
         rewrite(select(x, min(y, w), min(w, z)), min(select(x, y, z), w)) ||
         rewrite(select(x, min(w, y), min(z, w)), min(w, select(x, y, z))) ||
         rewrite(select(x, min(w, y), min(w, z)), min(w, select(x, y, z))) ||
         rewrite(select(x, max(y, w), max(z, w)), max(select(x, y, z), w)) ||
         rewrite(select(x, max(y, w), max(w, z)), max(select(x, y, z), w)) ||
         rewrite(select(x, max(w, y), max(z, w)), max(w, select(x, y, z))) ||
         rewrite(select(x, max(w, y), max(w, z)), max(w, select(x, y, z))) ||

         rewrite(select(0 < x, min(x*c0, c1), x*c0), min(x*c0, c1), c1 >= 0 && c0 >= 0) ||
         rewrite(select(x < c0, 0, min(x, c0) + c1), 0, c0 == -c1) ||
         rewrite(select(0 < x, ((x*c0) + c1) / x, y), select(0 < x, c0 - 1, y), c1 == -1) ||

         rewrite(select(x, select(y, z, min(w, z)), min(u, z)), min(select(x, select(y, z, w), u), z)) ||
         rewrite(select(x, select(y, min(w, z), z), min(u, z)), min(select(x, select(y, w, z), u), z)) ||
         rewrite(select(x, min(u, z), select(y, z, min(w, z))), min(select(x, u, select(y, z, w)), z)) ||
         rewrite(select(x, min(u, z), select(y, min(w, z), z)), min(select(x, u, select(y, w, z)), z)) ||
         rewrite(select(x, select(y, z, min(w, z)), min(z, u)), min(select(x, select(y, z, w), u), z)) ||
         rewrite(select(x, select(y, min(w, z), z), min(z, u)), min(select(x, select(y, w, z), u), z)) ||
         rewrite(select(x, min(z, u), select(y, z, min(w, z))), min(select(x, u, select(y, z, w)), z)) ||
         rewrite(select(x, min(z, u), select(y, min(w, z), z)), min(select(x, u, select(y, w, z)), z)) ||
         rewrite(select(x, select(y, z, min(z, w)), min(u, z)), min(select(x, select(y, z, w), u), z)) ||
         rewrite(select(x, select(y, min(z, w), z), min(u, z)), min(select(x, select(y, w, z), u), z)) ||
         rewrite(select(x, min(u, z), select(y, z, min(z, w))), min(select(x, u, select(y, z, w)), z)) ||
         rewrite(select(x, min(u, z), select(y, min(z, w), z)), min(select(x, u, select(y, w, z)), z)) ||
         rewrite(select(x, select(y, z, min(z, w)), min(z, u)), min(select(x, select(y, z, w), u), z)) ||
         rewrite(select(x, select(y, min(z, w), z), min(z, u)), min(select(x, select(y, w, z), u), z)) ||
         rewrite(select(x, min(z, u), select(y, z, min(z, w))), min(select(x, u, select(y, z, w)), z)) ||
         rewrite(select(x, min(z, u), select(y, min(z, w), z)), min(select(x, u, select(y, w, z)), z)) ||

         rewrite(select(x, select(y, z, max(w, z)), max(u, z)), max(select(x, select(y, z, w), u), z)) ||
         rewrite(select(x, select(y, max(w, z), z), max(u, z)), max(select(x, select(y, w, z), u), z)) ||
         rewrite(select(x, max(u, z), select(y, z, max(w, z))), max(select(x, u, select(y, z, w)), z)) ||
         rewrite(select(x, max(u, z), select(y, max(w, z), z)), max(select(x, u, select(y, w, z)), z)) ||
         rewrite(select(x, select(y, z, max(w, z)), max(z, u)), max(select(x, select(y, z, w), u), z)) ||
         rewrite(select(x, select(y, max(w, z), z), max(z, u)), max(select(x, select(y, w, z), u), z)) ||
         rewrite(select(x, max(z, u), select(y, z, max(w, z))), max(select(x, u, select(y, z, w)), z)) ||
         rewrite(select(x, max(z, u), select(y, max(w, z), z)), max(select(x, u, select(y, w, z)), z)) ||
         rewrite(select(x, select(y, z, max(z, w)), max(u, z)), max(select(x, select(y, z, w), u), z)) ||
         rewrite(select(x, select(y, max(z, w), z), max(u, z)), max(select(x, select(y, w, z), u), z)) ||
         rewrite(select(x, max(u, z), select(y, z, max(z, w))), max(select(x, u, select(y, z, w)), z)) ||
         rewrite(select(x, max(u, z), select(y, max(z, w), z)), max(select(x, u, select(y, w, z)), z)) ||
         rewrite(select(x, select(y, z, max(z, w)), max(z, u)), max(select(x, select(y, z, w), u), z)) ||
         rewrite(select(x, select(y, max(z, w), z), max(z, u)), max(select(x, select(y, w, z), u), z)) ||
         rewrite(select(x, max(z, u), select(y, z, max(z, w))), max(select(x, u, select(y, z, w)), z)) ||
         rewrite(select(x, max(z, u), select(y, max(z, w), z)), max(select(x, u, select(y, w, z)), z)) ||

         // Note that in the rules below we know y is not a
         // constant because it appears on the LHS of an
         // addition. These rules therefore trade a non-constant
         // for a constant.
         rewrite(select(x, y + z, y), y + select(x, z, 0)) ||
         rewrite(select(x, y, y + z), y + select(x, 0, z)) ||

         rewrite(select(x, y - z, y), y + select(x, 0 - z, 0), !is_const(y)) ||
         rewrite(select(x, y, y - z), y + select(x, 0, 0 - z), !is_const(y)) ||

         (no_overflow_int(op->type) &&
          (rewrite(select(x, y * c0, c1), select(x, y, fold(c1 / c0)) * c0, c1 % c0 == 0) ||
           rewrite(select(x, c0, y * c1), select(x, fold(c0 / c1), y) * c1, c0 % c1 == 0) ||
           rewrite(select(x, y + c0, c1), select(x, y, fold(c1 - c0)) + c0) ||

           // Selects that are equivalent to mins/maxes
           rewrite(select(c0 < x, x + c1, c2), max(x + c1, c2), c2 == c0 + c1 || c2 == c0 + c1 + 1) ||
           rewrite(select(x < c0, c1, x + c2), max(x + c2, c1), c1 == c0 + c2 || c1 + 1 == c0 + c2) ||
           rewrite(select(c0 < x, c1, x + c2), min(x + c2, c1), c1 == c0 + c2 || c1 == c0 + c2 + 1) ||
           rewrite(select(x < c0, x + c1, c2), min(x + c1, c2), c2 == c0 + c1 || c2 + 1 == c0 + c1) ||

           rewrite(select(c0 < x, x, c1), max(x, c1), c1 == c0 + 1) ||
           rewrite(select(x < c0, c1, x), max(x, c1), c1 + 1 == c0) ||
           rewrite(select(c0 < x, c1, x), min(x, c1), c1 == c0 + 1) ||
           rewrite(select(x < c0, x, c1), min(x, c1), c1 + 1 == c0))) ||

         (op->type.is_bool() &&
          (rewrite(select(x, true, false), cast(op->type, x)) ||
           rewrite(select(x, false, true), cast(op->type, !x)) ||
           rewrite(select(x, y, false), x && y) ||
           rewrite(select(x, y, true), !x || y) ||
           rewrite(select(x, false, y), !x && y) ||
           rewrite(select(x, true, y), x || y))))) {
        return mutate(rewrite.result, info);
    }
    // clang-format on

    if (condition.same_as(op->condition) &&
        true_value.same_as(op->true_value) &&
        false_value.same_as(op->false_value)) {
        return op;
    } else {
        return Select::make(std::move(condition), std::move(true_value), std::move(false_value));
    }
}

}  // namespace Internal
}  // namespace Halide
