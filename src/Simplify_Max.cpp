#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Max *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds) {
        bounds->min_defined = a_bounds.min_defined || b_bounds.min_defined;
        bounds->max_defined = a_bounds.max_defined && b_bounds.max_defined;
        bounds->max = std::max(a_bounds.max, b_bounds.max);
        if (a_bounds.min_defined && b_bounds.min_defined) {
            bounds->min = std::max(a_bounds.min, b_bounds.min);
        } else if (a_bounds.min_defined) {
            bounds->min = a_bounds.min;
        } else {
            bounds->min = b_bounds.min;
        }
        bounds->alignment = ModulusRemainder::unify(a_bounds.alignment, b_bounds.alignment);
        bounds->trim_bounds_using_alignment();
    }

    // Early out when the bounds tells us one side or the other is smaller
    if (a_bounds.max_defined && b_bounds.min_defined && a_bounds.max <= b_bounds.min) {
        if (const Call *call = b.as<Call>()) {
            if (call->is_intrinsic(Call::likely) ||
                call->is_intrinsic(Call::likely_if_innermost)) {
                return call->args[0];
            }
        }
        return b;
    }
    if (b_bounds.max_defined && a_bounds.min_defined && b_bounds.max <= a_bounds.min) {
        if (const Call *call = a.as<Call>()) {
            if (call->is_intrinsic(Call::likely) ||
                call->is_intrinsic(Call::likely_if_innermost)) {
                return call->args[0];
            }
        }
        return a;
    }

    if (may_simplify(op->type)) {

        // Order commutative operations by node type
        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_bounds, b_bounds);
        }

        int lanes = op->type.lanes();
        auto rewrite = IRMatcher::rewriter(IRMatcher::max(a, b), op->type);

        // clang-format off
        if (EVAL_IN_LAMBDA
            ((rewrite(max(x, x), x, "max47")) ||
             (rewrite(max(c0, c1), fold(max(c0, c1)), "max48")) ||
             (rewrite(max(IRMatcher::Overflow(), x), IRMatcher::Overflow(), "max49")) ||
             (rewrite(max(x,IRMatcher::Overflow()), IRMatcher::Overflow(), "max50")) ||
             // Cases where one side dominates:
             (rewrite(max(x, op->type.max()), b, "max52")) ||
             (rewrite(max(x, op->type.min()), x, "max53")) ||
             (rewrite(max((x/c0)*c0, x), x, c0 > 0, "max54")) ||
             (rewrite(max(x, (x/c0)*c0), x, c0 > 0, "max55")) ||
             (rewrite(max(max(x, y), x), max(x, y), "max56")) ||
             (rewrite(max(max(x, y), y), max(x, y), "max57")) ||
             (rewrite(max(max(max(x, y), z), x), max(max(x, y), z), "max58")) ||
             (rewrite(max(max(max(x, y), z), y), max(max(x, y), z), "max59")) ||
             (rewrite(max(max(max(max(x, y), z), w), x), max(max(max(x, y), z), w), "max60")) ||
             (rewrite(max(max(max(max(x, y), z), w), y), max(max(max(x, y), z), w), "max61")) ||
             (rewrite(max(max(max(max(max(x, y), z), w), u), x), max(max(max(max(x, y), z), w), u), "max62")) ||
             (rewrite(max(max(max(max(max(x, y), z), w), u), y), max(max(max(max(x, y), z), w), u), "max63")) ||
             (rewrite(max(x, min(x, y)), x, "max64")) ||
             (rewrite(max(x, min(y, x)), x, "max65")) ||
             (rewrite(max(max(x, y), min(x, y)), max(x, y), "max66")) ||
             (rewrite(max(max(x, y), min(y, x)), max(x, y), "max67")) ||
             (rewrite(max(min(x, y), x), x, "max68")) ||
             (rewrite(max(min(y, x), x), x, "max69")) ||
             (rewrite(max(min(x, c0), c1), c1, c1 >= c0, "max70")) ||

             (rewrite(max(intrin(Call::likely, x), x), intrin(Call::likely, x), "max72")) ||
             (rewrite(max(x, intrin(Call::likely, x)), intrin(Call::likely, x), "max73")) ||
             (rewrite(max(intrin(Call::likely_if_innermost, x), x), intrin(Call::likely_if_innermost, x), "max74")) ||
             (rewrite(max(x, intrin(Call::likely_if_innermost, x)), intrin(Call::likely_if_innermost, x), "max75")) ||

             (no_overflow(op->type) &&
              ((rewrite(max(ramp(x, y), broadcast(z)), ramp(x, y), can_prove(x + y * (lanes - 1) >= z && x >= z, this), "max78")) ||
               (rewrite(max(ramp(x, y), broadcast(z)), broadcast(z), can_prove(x + y * (lanes - 1) <= z && x <= z, this), "max79")) ||
               // Compare x to a stair-step function in x
               (rewrite(max(((x + c0)/c1)*c1 + c2, x), ((x + c0)/c1)*c1 + c2, c1 > 0 && c0 + c2 >= c1 - 1, "max81")) ||
               (rewrite(max(x, ((x + c0)/c1)*c1 + c2), ((x + c0)/c1)*c1 + c2, c1 > 0 && c0 + c2 >= c1 - 1, "max82")) ||
               (rewrite(max(((x + c0)/c1)*c1 + c2, x), x, c1 > 0 && c0 + c2 <= 0, "max83")) ||
               (rewrite(max(x, ((x + c0)/c1)*c1 + c2), x, c1 > 0 && c0 + c2 <= 0, "max84")) ||
               // Special cases where c0 or c2 is zero
               (rewrite(max((x/c1)*c1 + c2, x), (x/c1)*c1 + c2, c1 > 0 && c2 >= c1 - 1, "max86")) ||
               (rewrite(max(x, (x/c1)*c1 + c2), (x/c1)*c1 + c2, c1 > 0 && c2 >= c1 - 1, "max87")) ||
               (rewrite(max(((x + c0)/c1)*c1, x), ((x + c0)/c1)*c1, c1 > 0 && c0 >= c1 - 1, "max88")) ||
               (rewrite(max(x, ((x + c0)/c1)*c1), ((x + c0)/c1)*c1, c1 > 0 && c0 >= c1 - 1, "max89")) ||
               (rewrite(max((x/c1)*c1 + c2, x), x, c1 > 0 && c2 <= 0, "max90")) ||
               (rewrite(max(x, (x/c1)*c1 + c2), x, c1 > 0 && c2 <= 0, "max91")) ||
               (rewrite(max(((x + c0)/c1)*c1, x), x, c1 > 0 && c0 <= 0, "max92")) ||
               (rewrite(max(x, ((x + c0)/c1)*c1), x, c1 > 0 && c0 <= 0, "max93")))))) {
            return rewrite.result;
        }
        // clang-format on

        // clang-format off
        if (EVAL_IN_LAMBDA
            ((rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1))), "max100")) ||
             (rewrite(max(max(x, c0), y), max(max(x, y), c0), "max101")) ||
             (rewrite(max(max(x, y), max(x, z)), max(max(y, z), x), "max102")) ||
             (rewrite(max(max(y, x), max(x, z)), max(max(y, z), x), "max103")) ||
             (rewrite(max(max(x, y), max(z, x)), max(max(y, z), x), "max104")) ||
             (rewrite(max(max(y, x), max(z, x)), max(max(y, z), x), "max105")) ||
             (rewrite(max(max(x, y), max(z, w)), max(max(max(x, y), z), w), "max106")) ||
             (rewrite(max(broadcast(x), broadcast(y)), broadcast(max(x, y), lanes), "max107")) ||
             (rewrite(max(max(x, broadcast(y)), broadcast(z)), max(x, broadcast(max(y, z), lanes)), "max109")) ||
             (rewrite(max(min(x, y), min(x, z)), min(x, max(y, z)), "max110")) ||
             (rewrite(max(min(x, y), min(z, x)), min(x, max(y, z)), "max111")) ||
             (rewrite(max(min(y, x), min(x, z)), min(max(y, z), x), "max112")) ||
             (rewrite(max(min(y, x), min(z, x)), min(max(y, z), x), "max113")) ||
             (rewrite(max(min(max(x, y), z), y), max(min(x, z), y), "max114")) ||
             (rewrite(max(min(max(y, x), z), y), max(y, min(x, z)), "max115")) ||
             (rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1))), "max116")) ||

             (no_overflow(op->type) &&
              ((rewrite(max(max(x, y) + c0, x), max(x, y + c0), c0 < 0, "max119")) ||
               (rewrite(max(max(x, y) + c0, x), max(x, y) + c0, c0 > 0, "max120")) ||
               (rewrite(max(max(y, x) + c0, x), max(y + c0, x), c0 < 0, "max121")) ||
               (rewrite(max(max(y, x) + c0, x), max(y, x) + c0, c0 > 0, "max122")) ||

               (rewrite(max(x, max(x, y) + c0), max(x, y + c0), c0 < 0, "max124")) ||
               (rewrite(max(x, max(x, y) + c0), max(x, y) + c0, c0 > 0, "max125")) ||
               (rewrite(max(x, max(y, x) + c0), max(x, y + c0), c0 < 0, "max126")) ||
               (rewrite(max(x, max(y, x) + c0), max(x, y) + c0, c0 > 0, "max127")) ||

               (rewrite(max(x + c0, c1), max(x, fold(c1 - c0)) + c0, "max129")) ||

               (rewrite(max(x + c0, y + c1), max(x, y + fold(c1 - c0)) + c0, c1 > c0, "max131")) ||
               (rewrite(max(x + c0, y + c1), max(x + fold(c0 - c1), y) + c1, c0 > c1, "max132")) ||

               (rewrite(max(x + y, x + z), x + max(y, z), "max134")) ||
               (rewrite(max(x + y, z + x), x + max(y, z), "max135")) ||
               (rewrite(max(y + x, x + z), max(y, z) + x, "max136")) ||
               (rewrite(max(y + x, z + x), max(y, z) + x, "max137")) ||
               (rewrite(max(x, x + z), x + max(z, 0), "max138")) ||
               (rewrite(max(x, z + x), x + max(z, 0), "max139")) ||
               (rewrite(max(y + x, x), max(y, 0) + x, "max140")) ||
               (rewrite(max(x + y, x), x + max(y, 0), "max141")) ||

               (rewrite(max((x*c0 + y)*c1, x*c2 + z), max(y*c1, z) + x*c2, c0 * c1 == c2, "max143")) ||
               (rewrite(max((y + x*c0)*c1, x*c2 + z), max(y*c1, z) + x*c2, c0 * c1 == c2, "max144")) ||
               (rewrite(max((x*c0 + y)*c1, z + x*c2), max(y*c1, z) + x*c2, c0 * c1 == c2, "max145")) ||
               (rewrite(max((y + x*c0)*c1, z + x*c2), max(y*c1, z) + x*c2, c0 * c1 == c2, "max146")) ||

               (rewrite(max(max(x + y, z), x + w), max(x + max(y, w), z), "max148")) ||
               (rewrite(max(max(z, x + y), x + w), max(x + max(y, w), z), "max149")) ||
               (rewrite(max(max(x + y, z), w + x), max(x + max(y, w), z), "max150")) ||
               (rewrite(max(max(z, x + y), w + x), max(x + max(y, w), z), "max151")) ||

               (rewrite(max(max(y + x, z), x + w), max(max(y, w) + x, z), "max153")) ||
               (rewrite(max(max(z, y + x), x + w), max(max(y, w) + x, z), "max154")) ||
               (rewrite(max(max(y + x, z), w + x), max(max(y, w) + x, z), "max155")) ||
               (rewrite(max(max(z, y + x), w + x), max(max(y, w) + x, z), "max156")) ||

               (rewrite(max((x + w) + y, x + z), x + max(w + y, z), "max158")) ||
               (rewrite(max((w + x) + y, x + z), max(w + y, z) + x, "max159")) ||
               (rewrite(max((x + w) + y, z + x), x + max(w + y, z), "max160")) ||
               (rewrite(max((w + x) + y, z + x), max(w + y, z) + x, "max161")) ||
               (rewrite(max((x + w) + y, x), x + max(w + y, 0), "max162")) ||
               (rewrite(max((w + x) + y, x), x + max(w + y, 0), "max163")) ||
               (rewrite(max(x + y, (w + x) + z), x + max(w + z, y), "max164")) ||
               (rewrite(max(x + y, (x + w) + z), x + max(w + z, y), "max165")) ||
               (rewrite(max(y + x, (w + x) + z), max(w + z, y) + x, "max166")) ||
               (rewrite(max(y + x, (x + w) + z), max(w + z, y) + x, "max167")) ||
               (rewrite(max(x, (w + x) + z), x + max(w + z, 0), "max168")) ||
               (rewrite(max(x, (x + w) + z), x + max(w + z, 0), "max169")) ||

               (rewrite(max(y - x, z - x), max(y, z) - x, "max171")) ||
               (rewrite(max(x - y, x - z), x - min(y, z), "max172")) ||

               (rewrite(max(x, x - y), x - min(y, 0), "max174")) ||
               (rewrite(max(x - y, x), x - min(y, 0), "max175")) ||
               (rewrite(max(x, (x - y) + z), x + max(z - y, 0), "max176")) ||
               (rewrite(max(x, z + (x - y)), x + max(z - y, 0), "max177")) ||
               (rewrite(max(x, (x - y) - z), x - min(y + z, 0), "max178")) ||
               (rewrite(max((x - y) + z, x), max(z - y, 0) + x, "max179")) ||
               (rewrite(max(z + (x - y), x), max(z - y, 0) + x, "max180")) ||
               (rewrite(max((x - y) - z, x), x - min(y + z, 0), "max181")) ||

               (rewrite(max(x * c0, c1), max(x, fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0, "max183")) ||
               (rewrite(max(x * c0, c1), min(x, fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0, "max184")) ||

               (rewrite(max(x * c0, y * c1), max(x, y * fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0, "max186")) ||
               (rewrite(max(x * c0, y * c1), min(x, y * fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0, "max187")) ||
               (rewrite(max(x * c0, y * c1), max(x * fold(c0 / c1), y) * c1, c1 > 0 && c0 % c1 == 0, "max188")) ||
               (rewrite(max(x * c0, y * c1), min(x * fold(c0 / c1), y) * c1, c1 < 0 && c0 % c1 == 0, "max189")) ||
               (rewrite(max(x * c0, y * c0 + c1), max(x, y + fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0, "max190")) ||
               (rewrite(max(x * c0, y * c0 + c1), min(x, y + fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0, "max191")) ||

               (rewrite(max(x / c0, y / c0), max(x, y) / c0, c0 > 0, "max193")) ||
               (rewrite(max(x / c0, y / c0), min(x, y) / c0, c0 < 0, "max194")) ||

               /* Causes some things to cancel, but also creates large constants and breaks peephole patterns
                  rewrite(max(x / c0, c1), max(x, fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0)) ||
                  rewrite(max(x / c0, c1), min(x, fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0)) ||
               */

               (rewrite(max(x / c0, y / c0 + c1), max(x, y + fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0), "max201")) ||
               (rewrite(max(x / c0, y / c0 + c1), min(x, y + fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0), "max202")) ||

               (rewrite(max(select(x, y, z), select(x, w, u)), select(x, max(y, w), max(z, u)), "max204")) ||

               (rewrite(max(c0 - x, c1), c0 - min(x, fold(c0 - c1)), "max206")))))) {

            return mutate(std::move(rewrite.result), bounds);
        }
        // clang-format on
    }

    const Shuffle *shuffle_a = a.as<Shuffle>();
    const Shuffle *shuffle_b = b.as<Shuffle>();
    if (shuffle_a && shuffle_b &&
        shuffle_a->is_slice() &&
        shuffle_b->is_slice()) {
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return hoist_slice_vector<Max>(op);
        } else {
            return hoist_slice_vector<Max>(Max::make(a, b));
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Max::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
