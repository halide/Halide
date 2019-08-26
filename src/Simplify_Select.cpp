#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Select *op, ExprInfo *bounds) {

    ExprInfo t_bounds, f_bounds;
    Expr condition = mutate(op->condition, nullptr);
    Expr true_value = mutate(op->true_value, &t_bounds);
    Expr false_value = mutate(op->false_value, &f_bounds);

    if (bounds) {
        bounds->min_defined = t_bounds.min_defined && f_bounds.min_defined;
        bounds->max_defined = t_bounds.max_defined && f_bounds.max_defined;
        bounds->min = std::min(t_bounds.min, f_bounds.min);
        bounds->max = std::max(t_bounds.max, f_bounds.max);
        bounds->alignment = ModulusRemainder::unify(t_bounds.alignment, f_bounds.alignment);
        bounds->trim_bounds_using_alignment();
    }

    if (may_simplify(op->type)) {
        auto rewrite = IRMatcher::rewriter(IRMatcher::select(condition, true_value, false_value), op->type);

        if (EVAL_IN_LAMBDA
            (rewrite(select(IRMatcher::intrin(Call::likely, true), x, y), x) ||
             rewrite(select(IRMatcher::intrin(Call::likely, false), x, y), y) ||
             rewrite(select(IRMatcher::intrin(Call::likely_if_innermost, true), x, y), x) ||
             rewrite(select(IRMatcher::intrin(Call::likely_if_innermost, false), x, y), y) ||
             rewrite(select(1, x, y), x) ||
             rewrite(select(0, x, y), y) ||
             rewrite(select(x, y, y), y) ||
             rewrite(select(x, intrin(Call::likely, y), y), true_value) ||
             rewrite(select(x, y, intrin(Call::likely, y)), false_value) ||
             rewrite(select(x, intrin(Call::likely_if_innermost, y), y), true_value) ||
             rewrite(select(x, y, intrin(Call::likely_if_innermost, y)), false_value) ||

             // Select evaluates both sides, so if we have an
             // unreachable expression on one side we can't use a
             // signalling error. Call it UB and assume it can't
             // happen. The tricky case to consider is:
             // select(x > 0, a/x, select(x < 0, b/x, indeterminate()))
             // If we use a signalling error and x > 0, then this will
             // evaluate indeterminate(), because the top-level select
             // evaluates both sides.

             rewrite(select(x, y, IRMatcher::Indeterminate()), y) ||
             rewrite(select(x, IRMatcher::Indeterminate(), y), y))) {
            return rewrite.result;
        }

        if (EVAL_IN_LAMBDA
            (rewrite(select(broadcast(x), y, z), select(x, y, z)) ||
             rewrite(select(x != y, z, w), select(x == y, w, z)) ||
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

             rewrite(select(x < y, x, y), min(x, y)) ||
             rewrite(select(x < y, y, x), max(x, y)) ||
             rewrite(select(x < 0, x * y, 0), min(x, 0) * y) ||
             rewrite(select(x < 0, 0, x * y), max(x, 0) * y) ||

             (no_overflow_int(op->type) &&
              (rewrite(select(x, y * c0, c1), select(x, y, fold(c1 / c0)) * c0, c1 % c0 == 0) ||
               rewrite(select(x, c0, y * c1), select(x, fold(c0 / c1), y) * c1, c0 % c1 == 0) ||

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
            return mutate(std::move(rewrite.result), bounds);
        }
    }

    if (condition.same_as(op->condition) &&
        true_value.same_as(op->true_value) &&
        false_value.same_as(op->false_value)) {
        return op;
    } else {
        return Select::make(std::move(condition), std::move(true_value), std::move(false_value));
    }

}

}
}
