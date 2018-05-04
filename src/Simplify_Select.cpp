#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Select *op, ConstBounds *bounds) {

    ConstBounds t_bounds, f_bounds;
    Expr condition = mutate(op->condition, nullptr);
    Expr true_value, false_value;
    {
        auto fact = scoped_truth(condition);
        true_value = mutate(op->true_value, &t_bounds);
    }
    {
        auto fact = scoped_falsehood(condition);
        false_value = mutate(op->false_value, &f_bounds);
    }

    if (bounds) {
        bounds->min_defined = t_bounds.min_defined && f_bounds.min_defined;
        bounds->max_defined = t_bounds.max_defined && f_bounds.max_defined;
        bounds->min = std::min(t_bounds.min, f_bounds.min);
        bounds->max = std::max(t_bounds.max, f_bounds.max);
    }

    if (may_simplify(op->type)) {
        auto rewrite = IRMatcher::rewriter(IRMatcher::select(condition, true_value, false_value));

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
             rewrite(select(x, y, intrin(Call::likely_if_innermost, y)), false_value))) {
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
