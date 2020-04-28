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

        // clang-format off
        if (EVAL_IN_LAMBDA
            ((rewrite(select(IRMatcher::intrin(Call::likely, true), x, y), x, "sel27")) ||
             (rewrite(select(IRMatcher::intrin(Call::likely, false), x, y), y, "sel28")) ||
             (rewrite(select(IRMatcher::intrin(Call::likely_if_innermost, true), x, y), x, "sel29")) ||
             (rewrite(select(IRMatcher::intrin(Call::likely_if_innermost, false), x, y), y, "sel30")) ||
             (rewrite(select(1, x, y), x, "sel31")) ||
             (rewrite(select(0, x, y), y, "sel32")) ||
             (rewrite(select(x, y, y), y, "sel33")) ||
             (rewrite(select(x, intrin(Call::likely, y), y), intrin(Call::likely, y), "sel34")) ||
             (rewrite(select(x, y, intrin(Call::likely, y)), intrin(Call::likely, y), "sel35")) ||
             (rewrite(select(x, intrin(Call::likely_if_innermost, y), y), intrin(Call::likely_if_innermost, y), "sel36")) ||
             (rewrite(select(x, y, intrin(Call::likely_if_innermost, y)), intrin(Call::likely_if_innermost, y), "sel37")) ||
             false)) {
            return rewrite.result;
        }
        // clang-format on

        // clang-format off
        if (EVAL_IN_LAMBDA
            ((rewrite(select(broadcast(x), y, z), select(x, y, z), "sel45")) ||
             (rewrite(select(x != y, z, w), select(x == y, w, z), "sel46")) ||
             (rewrite(select(x <= y, z, w), select(y < x, w, z), "sel47")) ||
             (rewrite(select(x, select(y, z, w), z), select(x && !y, w, z), "sel48")) ||
             (rewrite(select(x, select(y, z, w), w), select(x && y, z, w), "sel49")) ||
             (rewrite(select(x, y, select(z, y, w)), select(x || z, y, w), "sel50")) ||
             (rewrite(select(x, y, select(z, w, y)), select(x || !z, y, w), "sel51")) ||
             (rewrite(select(x, select(x, y, z), w), select(x, y, w), "sel52")) ||
             (rewrite(select(x, y, select(x, z, w)), select(x, y, w), "sel53")) ||
             (rewrite(select(x, y + z, y + w), y + select(x, z, w), "sel54")) ||
             (rewrite(select(x, y + z, w + y), y + select(x, z, w), "sel55")) ||
             (rewrite(select(x, z + y, y + w), y + select(x, z, w), "sel56")) ||
             (rewrite(select(x, z + y, w + y), select(x, z, w) + y, "sel57")) ||
             (rewrite(select(x, y - z, y - w), y - select(x, z, w), "sel58")) ||
             (rewrite(select(x, y - z, y + w), y + select(x, -z, w), "sel59")) ||
             (rewrite(select(x, y + z, y - w), y + select(x, z, -w), "sel60")) ||
             (rewrite(select(x, y - z, w + y), y + select(x, -z, w), "sel61")) ||
             (rewrite(select(x, z + y, y - w), y + select(x, z, -w), "sel62")) ||
             (rewrite(select(x, z - y, w - y), select(x, z, w) - y, "sel63")) ||
             (rewrite(select(x, y * z, y * w), y * select(x, z, w), "sel64")) ||
             (rewrite(select(x, y * z, w * y), y * select(x, z, w), "sel65")) ||
             (rewrite(select(x, z * y, y * w), y * select(x, z, w), "sel66")) ||
             (rewrite(select(x, z * y, w * y), select(x, z, w) * y, "sel67")) ||
             (rewrite(select(x, z / y, w / y), select(x, z, w) / y, "sel68")) ||
             (rewrite(select(x, z % y, w % y), select(x, z, w) % y, "sel69")) ||

             (rewrite(select(x, (y + z) + u, y + w), y + select(x, z + u, w), "sel71")) ||
             (rewrite(select(x, (y + z) - u, y + w), y + select(x, z - u, w), "sel72")) ||
             (rewrite(select(x, u + (y + z), y + w), y + select(x, u + z, w), "sel73")) ||
             (rewrite(select(x, y + z, (y + w) + u), y + select(x, z, w + u), "sel74")) ||
             (rewrite(select(x, y + z, (y + w) - u), y + select(x, z, w - u), "sel75")) ||
             (rewrite(select(x, y + z, u + (y + w)), y + select(x, z, u + w), "sel76")) ||

             (rewrite(select(x, (y + z) + u, w + y), y + select(x, z + u, w), "sel78")) ||
             (rewrite(select(x, (y + z) - u, w + y), y + select(x, z - u, w), "sel79")) ||
             (rewrite(select(x, u + (y + z), w + y), y + select(x, u + z, w), "sel80")) ||
             (rewrite(select(x, y + z, (w + y) + u), y + select(x, z, w + u), "sel81")) ||
             (rewrite(select(x, y + z, (w + y) - u), y + select(x, z, w - u), "sel82")) ||
             (rewrite(select(x, y + z, u + (w + y)), y + select(x, z, u + w), "sel83")) ||

             (rewrite(select(x, (z + y) + u, y + w), y + select(x, z + u, w), "sel85")) ||
             (rewrite(select(x, (z + y) - u, y + w), y + select(x, z - u, w), "sel86")) ||
             (rewrite(select(x, u + (z + y), y + w), y + select(x, u + z, w), "sel87")) ||
             (rewrite(select(x, z + y, (y + w) + u), y + select(x, z, w + u), "sel88")) ||
             (rewrite(select(x, z + y, (y + w) - u), y + select(x, z, w - u), "sel89")) ||
             (rewrite(select(x, z + y, u + (y + w)), y + select(x, z, u + w), "sel90")) ||

             (rewrite(select(x, (z + y) + u, w + y), select(x, z + u, w) + y, "sel92")) ||
             (rewrite(select(x, (z + y) - u, w + y), select(x, z - u, w) + y, "sel93")) ||
             (rewrite(select(x, u + (z + y), w + y), select(x, u + z, w) + y, "sel94")) ||
             (rewrite(select(x, z + y, (w + y) + u), select(x, z, w + u) + y, "sel95")) ||
             (rewrite(select(x, z + y, (w + y) - u), select(x, z, w - u) + y, "sel96")) ||
             (rewrite(select(x, z + y, u + (w + y)), select(x, z, u + w) + y, "sel97")) ||

             (rewrite(select(x < y, x, y), min(x, y), "sel99")) ||
             (rewrite(select(x < y, y, x), max(x, y), "sel100")) ||
             (rewrite(select(x < 0, x * y, 0), min(x, 0) * y, "sel101")) ||
             (rewrite(select(x < 0, 0, x * y), max(x, 0) * y, "sel102")) ||

             // Note that in the rules below we know y is not a
             // constant because it appears on the LHS of an
             // addition. These rules therefore trade a non-constant
             // for a constant.
             rewrite(select(x, y + z, y), y + select(x, z, 0), "sel200") ||
             rewrite(select(x, y, y + z), y + select(x, 0, z), "sel201") ||

             (no_overflow_int(op->type) &&
              ((rewrite(select(x, y * c0, c1), select(x, y, fold(c1 / c0)) * c0, c1 % c0 == 0, "sel105")) ||
               (rewrite(select(x, c0, y * c1), select(x, fold(c0 / c1), y) * c1, c0 % c1 == 0, "sel106")) ||

               // Selects that are equivalent to mins/maxes
               (rewrite(select(c0 < x, x + c1, c2), max(x + c1, c2), c2 == c0 + c1 || c2 == c0 + c1 + 1, "sel109")) ||
               (rewrite(select(x < c0, c1, x + c2), max(x + c2, c1), c1 == c0 + c2 || c1 + 1 == c0 + c2, "sel110")) ||
               (rewrite(select(c0 < x, c1, x + c2), min(x + c2, c1), c1 == c0 + c2 || c1 == c0 + c2 + 1, "sel111")) ||
               (rewrite(select(x < c0, x + c1, c2), min(x + c1, c2), c2 == c0 + c1 || c2 + 1 == c0 + c1, "sel112")) ||

               (rewrite(select(c0 < x, x, c1), max(x, c1), c1 == c0 + 1, "sel114")) ||
               (rewrite(select(x < c0, c1, x), max(x, c1), c1 + 1 == c0, "sel115")) ||
               (rewrite(select(c0 < x, c1, x), min(x, c1), c1 == c0 + 1, "sel116")) ||
               (rewrite(select(x < c0, x, c1), min(x, c1), c1 + 1 == c0, "sel117")))) ||

             (op->type.is_bool() &&
              ((rewrite(select(x, true, false), cast(op->type, x), "sel120")) ||
               (rewrite(select(x, false, true), cast(op->type, !x), "sel121")) ||
               (rewrite(select(x, y, false), x && y, "sel122")) ||
               (rewrite(select(x, y, true), !x || y, "sel123")) ||
               (rewrite(select(x, false, y), !x && y, "sel124")) ||
               (rewrite(select(x, true, y), x || y, "sel125")))))) {
            return mutate(std::move(rewrite.result), bounds);
        }
        // clang-format on

        Expr a = condition, b = true_value;
        if (no_overflow_int(op->type) &&
            use_synthesized_rules &&
            (
#include "Simplify_Select.inc"
                )) {
            return mutate(rewrite.result, bounds);
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

}  // namespace Internal
}  // namespace Halide
