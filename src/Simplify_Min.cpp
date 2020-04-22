#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Min *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds) {
        bounds->min_defined = a_bounds.min_defined && b_bounds.min_defined;
        bounds->max_defined = a_bounds.max_defined || b_bounds.max_defined;
        bounds->min = std::min(a_bounds.min, b_bounds.min);
        if (a_bounds.max_defined && b_bounds.max_defined) {
            bounds->max = std::min(a_bounds.max, b_bounds.max);
        } else if (a_bounds.max_defined) {
            bounds->max = a_bounds.max;
        } else {
            bounds->max = b_bounds.max;
        }
        bounds->alignment = ModulusRemainder::unify(a_bounds.alignment, b_bounds.alignment);
        bounds->trim_bounds_using_alignment();
    }

    // Early out when the bounds tells us one side or the other is smaller
    if (a_bounds.max_defined && b_bounds.min_defined && a_bounds.max <= b_bounds.min) {
        if (const Call *call = a.as<Call>()) {
            if (call->is_intrinsic(Call::likely) ||
                call->is_intrinsic(Call::likely_if_innermost)) {
                return call->args[0];
            }
        }
        return a;
    }
    if (b_bounds.max_defined && a_bounds.min_defined && b_bounds.max <= a_bounds.min) {
        if (const Call *call = b.as<Call>()) {
            if (call->is_intrinsic(Call::likely) ||
                call->is_intrinsic(Call::likely_if_innermost)) {
                return call->args[0];
            }
        }
        return b;
    }

    if (may_simplify(op->type)) {

        // Order commutative operations by node type
        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_bounds, b_bounds);
        }

        int lanes = op->type.lanes();
        auto rewrite = IRMatcher::rewriter(IRMatcher::min(a, b), op->type);

        // clang-format off
        if (EVAL_IN_LAMBDA
            ((rewrite(min(x, x), x, "min47")) ||
             (rewrite(min(c0, c1), fold(min(c0, c1)), "min48")) ||
             (rewrite(min(IRMatcher::Overflow(), x), IRMatcher::Overflow(), "min49")) ||
             (rewrite(min(x,IRMatcher::Overflow()), IRMatcher::Overflow(), "min50")) ||
             // Cases where one side dominates:
             (rewrite(min(x, op->type.min()), b, "min52")) ||
             (rewrite(min(x, op->type.max()), x, "min53")) ||
             (rewrite(min((x/c0)*c0, x), (x/c0)*c0, c0 > 0, "min54")) ||
             (rewrite(min(x, (x/c0)*c0), (x/c0)*c0, c0 > 0, "min55")) ||
             (rewrite(min(min(x, y), x), min(x, y), "min56")) ||
             (rewrite(min(min(x, y), y), min(x, y), "min57")) ||
             (rewrite(min(min(min(x, y), z), x), min(min(x, y), z), "min58")) ||
             (rewrite(min(min(min(x, y), z), y), min(min(x, y), z), "min59")) ||
             (rewrite(min(min(min(min(x, y), z), w), x), min(min(min(x, y), z), w), "min60")) ||
             (rewrite(min(min(min(min(x, y), z), w), y), min(min(min(x, y), z), w), "min61")) ||
             (rewrite(min(min(min(min(min(x, y), z), w), u), x), min(min(min(min(x, y), z), w), u), "min62")) ||
             (rewrite(min(min(min(min(min(x, y), z), w), u), y), min(min(min(min(x, y), z), w), u), "min63")) ||
             (rewrite(min(x, max(x, y)), x, "min64")) ||
             (rewrite(min(x, max(y, x)), x, "min65")) ||
             (rewrite(min(max(x, y), min(x, y)), min(x, y), "min66")) ||
             (rewrite(min(max(x, y), min(y, x)), min(y, x), "min67")) ||
             (rewrite(min(max(x, y), x), x, "min68")) ||
             (rewrite(min(max(y, x), x), x, "min69")) ||
             (rewrite(min(max(x, c0), c1), c1, c1 <= c0, "min70")) ||

             (rewrite(min(intrin(Call::likely, x), x), intrin(Call::likely, x), "min72")) ||
             (rewrite(min(x, intrin(Call::likely, x)), intrin(Call::likely, x), "min73")) ||
             (rewrite(min(intrin(Call::likely_if_innermost, x), x), intrin(Call::likely_if_innermost, x), "min74")) ||
             (rewrite(min(x, intrin(Call::likely_if_innermost, x)), intrin(Call::likely_if_innermost, x), "min75")) ||

             (no_overflow(op->type) &&
              ((rewrite(min(ramp(x, y), broadcast(z)), ramp(x, y), can_prove(x + y * (lanes - 1) <= z && x <= z, this), "min78")) ||
               (rewrite(min(ramp(x, y), broadcast(z)), broadcast(z), can_prove(x + y * (lanes - 1) >= z && x >= z, this), "min79")) ||
               // Compare x to a stair-step function in x
               (rewrite(min(((x + c0)/c1)*c1 + c2, x), x, c1 > 0 && c0 + c2 >= c1 - 1, "min81")) ||
               (rewrite(min(x, ((x + c0)/c1)*c1 + c2), x, c1 > 0 && c0 + c2 >= c1 - 1, "min82")) ||
               (rewrite(min(((x + c0)/c1)*c1 + c2, x), ((x + c0)/c1)*c1 + c2, c1 > 0 && c0 + c2 <= 0, "min83")) ||
               (rewrite(min(x, ((x + c0)/c1)*c1 + c2), ((x + c0)/c1)*c1 + c2, c1 > 0 && c0 + c2 <= 0, "min84")) ||
               // Special cases where c0 or c2 is zero
               (rewrite(min((x/c1)*c1 + c2, x), x, c1 > 0 && c2 >= c1 - 1, "min86")) ||
               (rewrite(min(x, (x/c1)*c1 + c2), x, c1 > 0 && c2 >= c1 - 1, "min87")) ||
               (rewrite(min(((x + c0)/c1)*c1, x), x, c1 > 0 && c0 >= c1 - 1, "min88")) ||
               (rewrite(min(x, ((x + c0)/c1)*c1), x, c1 > 0 && c0 >= c1 - 1, "min89")) ||
               (rewrite(min((x/c1)*c1 + c2, x), (x/c1)*c1 + c2, c1 > 0 && c2 <= 0, "min90")) ||
               (rewrite(min(x, (x/c1)*c1 + c2), (x/c1)*c1 + c2, c1 > 0 && c2 <= 0, "min91")) ||
               (rewrite(min(((x + c0)/c1)*c1, x), ((x + c0)/c1)*c1, c1 > 0 && c0 <= 0, "min92")) ||
               (rewrite(min(x, ((x + c0)/c1)*c1), ((x + c0)/c1)*c1, c1 > 0 && c0 <= 0, "min93")))))) {
            return rewrite.result;
        }
        // clang-format on

        // clang-format off
        if (EVAL_IN_LAMBDA
            ((rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1))), "min100")) ||
             (rewrite(min(min(x, c0), y), min(min(x, y), c0), "min101")) ||
             (rewrite(min(min(x, y), min(x, z)), min(min(y, z), x), "min102")) ||
             (rewrite(min(min(y, x), min(x, z)), min(min(y, z), x), "min103")) ||
             (rewrite(min(min(x, y), min(z, x)), min(min(y, z), x), "min104")) ||
             (rewrite(min(min(y, x), min(z, x)), min(min(y, z), x), "min105")) ||
             (rewrite(min(min(x, y), min(z, w)), min(min(min(x, y), z), w), "min106")) ||
             (rewrite(min(broadcast(x), broadcast(y)), broadcast(min(x, y), lanes), "min107")) ||
             (rewrite(min(min(x, broadcast(y)), broadcast(z)), min(x, broadcast(min(y, z), lanes)), "min109")) ||
             (rewrite(min(max(x, y), max(x, z)), max(x, min(y, z)), "min110")) ||
             (rewrite(min(max(x, y), max(z, x)), max(x, min(y, z)), "min111")) ||
             (rewrite(min(max(y, x), max(x, z)), max(min(y, z), x), "min112")) ||
             (rewrite(min(max(y, x), max(z, x)), max(min(y, z), x), "min113")) ||
             (rewrite(min(max(min(x, y), z), y), min(max(x, z), y), "min114")) ||
             (rewrite(min(max(min(y, x), z), y), min(y, max(x, z)), "min115")) ||
             (rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1))), "min116")) ||

             // Canonicalize a clamp
             (rewrite(min(max(x, c0), c1), max(min(x, c1), c0), c0 <= c1, "min119")) ||

             (no_overflow(op->type) &&
              ((rewrite(min(min(x, y) + c0, x), min(x, y + c0), c0 > 0, "min122")) ||
               (rewrite(min(min(x, y) + c0, x), min(x, y) + c0, c0 < 0, "min123")) ||
               (rewrite(min(min(y, x) + c0, x), min(y + c0, x), c0 > 0, "min124")) ||
               (rewrite(min(min(y, x) + c0, x), min(y, x) + c0, c0 < 0, "min125")) ||

               (rewrite(min(x, min(x, y) + c0), min(x, y + c0), c0 > 0, "min127")) ||
               (rewrite(min(x, min(x, y) + c0), min(x, y) + c0, c0 < 0, "min128")) ||
               (rewrite(min(x, min(y, x) + c0), min(x, y + c0), c0 > 0, "min129")) ||
               (rewrite(min(x, min(y, x) + c0), min(x, y) + c0, c0 < 0, "min130")) ||

               (rewrite(min(x + c0, c1), min(x, fold(c1 - c0)) + c0, "min132")) ||

               (rewrite(min(x + c0, y + c1), min(x, y + fold(c1 - c0)) + c0, c1 > c0, "min134")) ||
               (rewrite(min(x + c0, y + c1), min(x + fold(c0 - c1), y) + c1, c0 > c1, "min135")) ||

               (rewrite(min(x + y, x + z), x + min(y, z), "min137")) ||
               (rewrite(min(x + y, z + x), x + min(y, z), "min138")) ||
               (rewrite(min(y + x, x + z), min(y, z) + x, "min139")) ||
               (rewrite(min(y + x, z + x), min(y, z) + x, "min140")) ||
               (rewrite(min(x, x + z), x + min(z, 0), "min141")) ||
               (rewrite(min(x, z + x), x + min(z, 0), "min142")) ||
               (rewrite(min(y + x, x), min(y, 0) + x, "min143")) ||
               (rewrite(min(x + y, x), x + min(y, 0), "min144")) ||

               (rewrite(min((x*c0 + y)*c1, x*c2 + z), min(y*c1, z) + x*c2, c0 * c1 == c2, "min146")) ||
               (rewrite(min((y + x*c0)*c1, x*c2 + z), min(y*c1, z) + x*c2, c0 * c1 == c2, "min147")) ||
               (rewrite(min((x*c0 + y)*c1, z + x*c2), min(y*c1, z) + x*c2, c0 * c1 == c2, "min148")) ||
               (rewrite(min((y + x*c0)*c1, z + x*c2), min(y*c1, z) + x*c2, c0 * c1 == c2, "min149")) ||

               (rewrite(min(min(x + y, z), x + w), min(x + min(y, w), z), "min151")) ||
               (rewrite(min(min(z, x + y), x + w), min(x + min(y, w), z), "min152")) ||
               (rewrite(min(min(x + y, z), w + x), min(x + min(y, w), z), "min153")) ||
               (rewrite(min(min(z, x + y), w + x), min(x + min(y, w), z), "min154")) ||

               (rewrite(min(min(y + x, z), x + w), min(min(y, w) + x, z), "min156")) ||
               (rewrite(min(min(z, y + x), x + w), min(min(y, w) + x, z), "min157")) ||
               (rewrite(min(min(y + x, z), w + x), min(min(y, w) + x, z), "min158")) ||
               (rewrite(min(min(z, y + x), w + x), min(min(y, w) + x, z), "min159")) ||

               (rewrite(min((x + w) + y, x + z), x + min(w + y, z), "min161")) ||
               (rewrite(min((w + x) + y, x + z), min(w + y, z) + x, "min162")) ||
               (rewrite(min((x + w) + y, z + x), x + min(w + y, z), "min163")) ||
               (rewrite(min((w + x) + y, z + x), min(w + y, z) + x, "min164")) ||
               (rewrite(min((x + w) + y, x), x + min(w + y, 0), "min165")) ||
               (rewrite(min((w + x) + y, x), x + min(w + y, 0), "min166")) ||
               (rewrite(min(x + y, (w + x) + z), x + min(w + z, y), "min167")) ||
               (rewrite(min(x + y, (x + w) + z), x + min(w + z, y), "min168")) ||
               (rewrite(min(y + x, (w + x) + z), min(w + z, y) + x, "min169")) ||
               (rewrite(min(y + x, (x + w) + z), min(w + z, y) + x, "min170")) ||
               (rewrite(min(x, (w + x) + z), x + min(w + z, 0), "min171")) ||
               (rewrite(min(x, (x + w) + z), x + min(w + z, 0), "min172")) ||

               (rewrite(min(y - x, z - x), min(y, z) - x, "min174")) ||
               (rewrite(min(x - y, x - z), x - max(y, z), "min175")) ||

               rewrite(min(x, x - y), x - max(y, 0), "min177") ||
               rewrite(min(x - y, x), x - max(y, 0), "min178") ||
               rewrite(min(x, (x - y) + z), x + min(z - y, 0), "min179") ||
               rewrite(min(x, z + (x - y)), x + min(z - y, 0), "min180") ||
               rewrite(min(x, (x - y) - z), x - max(y + z, 0), "min181") ||
               rewrite(min((x - y) + z, x), min(z - y, 0) + x, "min182") ||
               rewrite(min(z + (x - y), x), min(z - y, 0) + x, "min183") ||
               rewrite(min((x - y) - z, x), x - max(y + z, 0), "min184") ||

               (rewrite(min(x * c0, c1), min(x, fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0, "min186")) ||
               (rewrite(min(x * c0, c1), max(x, fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0, "min187")) ||

               (rewrite(min(x * c0, y * c1), min(x, y * fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0, "min189")) ||
               (rewrite(min(x * c0, y * c1), max(x, y * fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0, "min190")) ||
               (rewrite(min(x * c0, y * c1), min(x * fold(c0 / c1), y) * c1, c1 > 0 && c0 % c1 == 0, "min191")) ||
               (rewrite(min(x * c0, y * c1), max(x * fold(c0 / c1), y) * c1, c1 < 0 && c0 % c1 == 0, "min192")) ||
               (rewrite(min(x * c0, y * c0 + c1), min(x, y + fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0, "min193")) ||
               (rewrite(min(x * c0, y * c0 + c1), max(x, y + fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0, "min194")) ||

               (rewrite(min(x / c0, y / c0), min(x, y) / c0, c0 > 0, "min196")) ||
               (rewrite(min(x / c0, y / c0), max(x, y) / c0, c0 < 0, "min197")) ||

               /* Causes some things to cancel, but also creates large constants and breaks peephole patterns
               rewrite(min(x / c0, c1), min(x, fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0)) ||
               rewrite(min(x / c0, c1), max(x, fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0)) ||
               */

               (rewrite(min(x / c0, y / c0 + c1), min(x, y + fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0), "min204")) ||
               (rewrite(min(x / c0, y / c0 + c1), max(x, y + fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0), "min205")) ||

               (rewrite(min(select(x, y, z), select(x, w, u)), select(x, min(y, w), min(z, u)), "min207")) ||

               (rewrite(min(c0 - x, c1), c0 - max(x, fold(c0 - c1)), "min209")) ||

               // Required for nested GuardWithIf tilings
               rewrite(min((min(((y + c0)/c1), x)*c1), y + c2), min(x * c1, y + c2), c1 > 0 && c1 + c2 <= c0 + 1, "min210") ||
               rewrite(min((min(((y + c0)/c1), x)*c1) + c2, y), min(x * c1 + c2, y), c1 > 0 && c1 <= c0 + c2 + 1, "min211") ||
               false )))) {

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
            return hoist_slice_vector<Min>(op);
        } else {
            return hoist_slice_vector<Min>(Min::make(a, b));
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Min::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
