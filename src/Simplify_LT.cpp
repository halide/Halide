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
            ((rewrite(c0 < c1, fold(c0 < c1), "lt35")) ||
             (rewrite(x < x, false, "lt36")) ||
             (rewrite(x < ty.min(), false, "lt37")) ||
             (rewrite(ty.max() < x, false, "lt38")) ||

             (rewrite(max(x, y) < x, false, "lt40")) ||
             (rewrite(max(y, x) < x, false, "lt41")) ||
             (rewrite(x < min(x, y), false, "lt42")) ||
             (rewrite(x < min(y, x), false, "lt43")) ||

             // From the simplifier synthesis project
             rewrite((max(y, z) < min(x, y)), false, "lt500") ||
             rewrite((max(y, z) < min(y, x)), false, "lt501") ||
             rewrite((max(z, y) < min(x, y)), false, "lt502") ||
             rewrite((max(z, y) < min(y, x)), false, "lt503") ||

             // Comparisons of ramps and broadcasts. If the first
             // and last lanes are provably < or >= the broadcast
             // we can collapse the comparison.
             (no_overflow(op->type) &&
              ((rewrite(ramp(x, c1) < broadcast(z), true, can_prove(x + fold(max(0, c1 * (lanes - 1))) < z, this), "lt49")) ||
               (rewrite(ramp(x, c1) < broadcast(z), false, can_prove(x + fold(min(0, c1 * (lanes - 1))) >= z, this), "lt50")) ||
               (rewrite(broadcast(z) < ramp(x, c1), true, can_prove(z < x + fold(min(0, c1 * (lanes - 1))), this), "lt51")) ||
               (rewrite(broadcast(z) < ramp(x, c1), false, can_prove(z >= x + fold(max(0, c1 * (lanes - 1))), this), "lt52")))))){
            return rewrite.result;
        }
        // clang-format on

        // clang-format off
        if ((rewrite(broadcast(x) < broadcast(y), broadcast(x < y, lanes), "lt58")) ||
            (no_overflow(ty) && EVAL_IN_LAMBDA
             ((rewrite(ramp(x, y) < ramp(z, y), broadcast(x < z, lanes), "lt60")) ||
              // Move constants to the RHS
              (rewrite(x + c0 < y, x < y + fold(-c0), "lt62")) ||

              // Merge RHS constant additions with a constant LHS
              (rewrite(c0 < x + c1, fold(c0 - c1) < x, "lt65")) ||

              // Normalize subtractions to additions to cut down on cases to consider
              (rewrite(x - y < z, x < z + y, "lt68")) ||
              (rewrite(z < x - y, z + y < x, "lt69")) ||

              (rewrite((x - y) + z < w, x + z < y + w, "lt71")) ||
              (rewrite(z + (x - y) < w, x + z < y + w, "lt72")) ||
              (rewrite(w < (x - y) + z, w + y < x + z, "lt73")) ||
              (rewrite(w < z + (x - y), w + y < x + z, "lt74")) ||

              (rewrite(((x - y) + z) + u < w, x + z + u < w + y, "lt76")) ||
              (rewrite((z + (x - y)) + u < w, x + z + u < w + y, "lt77")) ||
              (rewrite(u + ((x - y) + z) < w, x + z + u < w + y, "lt78")) ||
              (rewrite(u + (z + (x - y)) < w, x + z + u < w + y, "lt79")) ||

              (rewrite(w < ((x - y) + z) + u, w + y < x + z + u, "lt81")) ||
              (rewrite(w < (z + (x - y)) + u, w + y < x + z + u, "lt82")) ||
              (rewrite(w < u + ((x - y) + z), w + y < x + z + u, "lt83")) ||
              (rewrite(w < u + (z + (x - y)), w + y < x + z + u, "lt84")) ||

              // Cancellations in linear expressions
              // 1 < 2
              (rewrite(x < x + y, 0 < y, "lt88")) ||
              (rewrite(x < y + x, 0 < y, "lt89")) ||

              // 2 < 1
              (rewrite(x + y < x, y < 0, "lt92")) ||
              (rewrite(y + x < x, y < 0, "lt93")) ||

              // 2 < 2
              (rewrite(x + y < x + z, y < z, "lt96")) ||
              (rewrite(x + y < z + x, y < z, "lt97")) ||
              (rewrite(y + x < x + z, y < z, "lt98")) ||
              (rewrite(y + x < z + x, y < z, "lt99")) ||

              // 3 < 2
              (rewrite((x + y) + w < x + z, y + w < z, "lt102")) ||
              (rewrite((y + x) + w < x + z, y + w < z, "lt103")) ||
              (rewrite(w + (x + y) < x + z, y + w < z, "lt104")) ||
              (rewrite(w + (y + x) < x + z, y + w < z, "lt105")) ||
              (rewrite((x + y) + w < z + x, y + w < z, "lt106")) ||
              (rewrite((y + x) + w < z + x, y + w < z, "lt107")) ||
              (rewrite(w + (x + y) < z + x, y + w < z, "lt108")) ||
              (rewrite(w + (y + x) < z + x, y + w < z, "lt109")) ||

              // 2 < 3
              (rewrite(x + z < (x + y) + w, z < y + w, "lt112")) ||
              (rewrite(x + z < (y + x) + w, z < y + w, "lt113")) ||
              (rewrite(x + z < w + (x + y), z < y + w, "lt114")) ||
              (rewrite(x + z < w + (y + x), z < y + w, "lt115")) ||
              (rewrite(z + x < (x + y) + w, z < y + w, "lt116")) ||
              (rewrite(z + x < (y + x) + w, z < y + w, "lt117")) ||
              (rewrite(z + x < w + (x + y), z < y + w, "lt118")) ||
              (rewrite(z + x < w + (y + x), z < y + w, "lt119")) ||

              // 3 < 3
              (rewrite((x + y) + w < (x + z) + u, y + w < z + u, "lt122")) ||
              (rewrite((y + x) + w < (x + z) + u, y + w < z + u, "lt123")) ||
              (rewrite((x + y) + w < (z + x) + u, y + w < z + u, "lt124")) ||
              (rewrite((y + x) + w < (z + x) + u, y + w < z + u, "lt125")) ||
              (rewrite(w + (x + y) < (x + z) + u, y + w < z + u, "lt126")) ||
              (rewrite(w + (y + x) < (x + z) + u, y + w < z + u, "lt127")) ||
              (rewrite(w + (x + y) < (z + x) + u, y + w < z + u, "lt128")) ||
              (rewrite(w + (y + x) < (z + x) + u, y + w < z + u, "lt129")) ||
              (rewrite((x + y) + w < u + (x + z), y + w < z + u, "lt130")) ||
              (rewrite((y + x) + w < u + (x + z), y + w < z + u, "lt131")) ||
              (rewrite((x + y) + w < u + (z + x), y + w < z + u, "lt132")) ||
              (rewrite((y + x) + w < u + (z + x), y + w < z + u, "lt133")) ||
              (rewrite(w + (x + y) < u + (x + z), y + w < z + u, "lt134")) ||
              (rewrite(w + (y + x) < u + (x + z), y + w < z + u, "lt135")) ||
              (rewrite(w + (x + y) < u + (z + x), y + w < z + u, "lt136")) ||
              (rewrite(w + (y + x) < u + (z + x), y + w < z + u, "lt137")) ||

              // Cancel a multiplication
              (rewrite(x * c0 < y * c0, x < y, c0 > 0, "lt140")) ||
              (rewrite(x * c0 < y * c0, y < x, c0 < 0, "lt141")) ||

              (ty.is_int()   && (rewrite(x * c0 < c1, x < fold((c1 + c0 - 1) / c0), c0 > 0, "lt143"))) ||
              (ty.is_float() && (rewrite(x * c0 < c1, x < fold(c1 / c0), c0 > 0, "lt144"))) ||
              (rewrite(c1 < x * c0, fold(c1 / c0) < x, c0 > 0, "lt145")) ||

              // Multiply-out a division
              (rewrite(x / c0 < c1, x < c1 * c0, c0 > 0, "lt148")) ||
              (ty.is_int() && (rewrite(c0 < x / c1, fold((c0 + 1) * c1 - 1) < x, c1 > 0, "lt149"))) ||
              (ty.is_float() && (rewrite(c0 < x / c1, fold(c0 * c1) < x, c1 > 0, "lt150"))) ||

              // We want to break max(x, y) < z into x < z && y <
              // z in cases where one of those two terms is going
              // to fold.
              (rewrite(min(x + c0, y) < x + c1, y < x + c1 || fold(c0 < c1), "lt155")) ||
              (rewrite(min(y, x + c0) < x + c1, y < x + c1 || fold(c0 < c1), "lt156")) ||
              (rewrite(max(x + c0, y) < x + c1, y < x + c1 && fold(c0 < c1), "lt157")) ||
              (rewrite(max(y, x + c0) < x + c1, y < x + c1 && fold(c0 < c1), "lt158")) ||

              (rewrite(x < min(x + c0, y) + c1, x < y + c1 && fold(0 < c0 + c1), "lt160")) ||
              (rewrite(x < min(y, x + c0) + c1, x < y + c1 && fold(0 < c0 + c1), "lt161")) ||
              (rewrite(x < max(x + c0, y) + c1, x < y + c1 || fold(0 < c0 + c1), "lt162")) ||
              (rewrite(x < max(y, x + c0) + c1, x < y + c1 || fold(0 < c0 + c1), "lt163")) ||

              // Special cases where c0 == 0
              (rewrite(min(x, y) < x + c1, y < x + c1 || fold(0 < c1), "lt166")) ||
              (rewrite(min(y, x) < x + c1, y < x + c1 || fold(0 < c1), "lt167")) ||
              (rewrite(max(x, y) < x + c1, y < x + c1 && fold(0 < c1), "lt168")) ||
              (rewrite(max(y, x) < x + c1, y < x + c1 && fold(0 < c1), "lt169")) ||

              (rewrite(x < min(x, y) + c1, x < y + c1 && fold(0 < c1), "lt171")) ||
              (rewrite(x < min(y, x) + c1, x < y + c1 && fold(0 < c1), "lt172")) ||
              (rewrite(x < max(x, y) + c1, x < y + c1 || fold(0 < c1), "lt173")) ||
              (rewrite(x < max(y, x) + c1, x < y + c1 || fold(0 < c1), "lt174")) ||

              // Special cases where c1 == 0
              (rewrite(min(x + c0, y) < x, y < x || fold(c0 < 0), "lt177")) ||
              (rewrite(min(y, x + c0) < x, y < x || fold(c0 < 0), "lt178")) ||
              (rewrite(max(x + c0, y) < x, y < x && fold(c0 < 0), "lt179")) ||
              (rewrite(max(y, x + c0) < x, y < x && fold(c0 < 0), "lt180")) ||

              (rewrite(x < min(x + c0, y), x < y && fold(0 < c0), "lt182")) ||
              (rewrite(x < min(y, x + c0), x < y && fold(0 < c0), "lt183")) ||
              (rewrite(x < max(x + c0, y), x < y || fold(0 < c0), "lt184")) ||
              (rewrite(x < max(y, x + c0), x < y || fold(0 < c0), "lt185")) ||

              // Special cases where c0 == c1 == 0
              (rewrite(min(x, y) < x, y < x, "lt188")) ||
              (rewrite(min(y, x) < x, y < x, "lt189")) ||
              (rewrite(x < max(x, y), x < y, "lt190")) ||
              (rewrite(x < max(y, x), x < y, "lt191")) ||

              // Special case where x is constant
              (rewrite(min(y, c0) < c1, y < c1 || fold(c0 < c1), "lt194")) ||
              (rewrite(max(y, c0) < c1, y < c1 && fold(c0 < c1), "lt195")) ||
              (rewrite(c1 < min(y, c0), c1 < y && fold(c1 < c0), "lt196")) ||
              (rewrite(c1 < max(y, c0), c1 < y || fold(c1 < c0), "lt197")) ||

              // Cases where we can remove a min on one side because
              // one term dominates another. These rules were
              // synthesized then extended by hand.
              rewrite(min(z, y) < min(x, y), z < min(x, y), "lt400") ||
              rewrite(min(z, y) < min(y, x), z < min(y, x), "lt401") ||
              rewrite(min(z, y) < min(x, y + c0), min(z, y) < x, c0 > 0, "lt402") ||
              rewrite(min(z, y) < min(y + c0, x), min(z, y) < x, c0 > 0, "lt403") ||
              rewrite(min(z, y + c0) < min(x, y), min(z, y + c0) < x, c0 < 0, "lt404") ||
              rewrite(min(z, y + c0) < min(y, x), min(z, y + c0) < x, c0 < 0, "lt405") ||

              rewrite(min(y, z) < min(x, y), z < min(x, y), "lt406") ||
              rewrite(min(y, z) < min(y, x), z < min(y, x), "lt407") ||
              rewrite(min(y, z) < min(x, y + c0), min(z, y) < x, c0 > 0, "lt408") ||
              rewrite(min(y, z) < min(y + c0, x), min(z, y) < x, c0 > 0, "lt409") ||
              rewrite(min(y + c0, z) < min(x, y), min(z, y + c0) < x, c0 < 0, "lt410") ||
              rewrite(min(y + c0, z) < min(y, x), min(z, y + c0) < x, c0 < 0, "lt411") ||

              // Equivalents with max
              rewrite(max(z, y) < max(x, y), max(z, y) < x, "lt412") ||
              rewrite(max(z, y) < max(y, x), max(z, y) < x, "lt413") ||
              rewrite(max(z, y) < max(x, y + c0), max(z, y) < x, c0 < 0, "lt414") ||
              rewrite(max(z, y) < max(y + c0, x), max(z, y) < x, c0 < 0, "lt415") ||
              rewrite(max(z, y + c0) < max(x, y), max(z, y + c0) < x, c0 > 0, "lt416") ||
              rewrite(max(z, y + c0) < max(y, x), max(z, y + c0) < x, c0 > 0, "lt417") ||

              rewrite(max(y, z) < max(x, y), max(z, y) < x, "lt418") ||
              rewrite(max(y, z) < max(y, x), max(z, y) < x, "lt419") ||
              rewrite(max(y, z) < max(x, y + c0), max(z, y) < x, c0 < 0, "lt420") ||
              rewrite(max(y, z) < max(y + c0, x), max(z, y) < x, c0 < 0, "lt421") ||
              rewrite(max(y + c0, z) < max(x, y), max(z, y + c0) < x, c0 > 0, "lt422") ||
              rewrite(max(y + c0, z) < max(y, x), max(z, y + c0) < x, c0 > 0, "lt423") ||

              // Comparisons with selects:
              // x < select(c, t, f) == c && (x < t) || !c && (x < f)
              // This is profitable when x < t or x < f is statically provable
              (rewrite(x < select(y, x + c0, z), !y && (x < z), c0 <= 0, "lt202")) ||
              (rewrite(x < select(y, x + c0, z), y || (x < z), c0 > 0, "lt203")) ||
              (rewrite(x < select(y, z, x + c0), y && (x < z), c0 <= 0, "lt204")) ||
              (rewrite(x < select(y, z, x + c0), !y || (x < z), c0 > 0, "lt205")) ||

              (rewrite(x < select(y, x + c0, z) + c1, !y && (x < z + c1), c0 + c1 <= 0, "lt207")) ||
              (rewrite(x < select(y, x + c0, z) + c1, y || (x < z + c1), c0 + c1 > 0, "lt208")) ||
              (rewrite(x < select(y, z, x + c0) + c1, y && (x < z + c1), c0 + c1 <= 0, "lt209")) ||
              (rewrite(x < select(y, z, x + c0) + c1, !y || (x < z + c1), c0 + c1 > 0, "lt210")) ||

              (rewrite(select(y, x + c0, z) < x, !y && (z < x), c0 >= 0, "lt212")) ||
              (rewrite(select(y, x + c0, z) < x, y || (z < x), c0 < 0, "lt213")) ||
              (rewrite(select(y, z, x + c0) < x, y && (z < x), c0 >= 0, "lt214")) ||
              (rewrite(select(y, z, x + c0) < x, !y || (z < x), c0 < 0, "lt215")) ||

              (rewrite(select(y, x + c0, z) < x + c1, !y && (z < x + c1), c0 >= c1, "lt217")) ||
              (rewrite(select(y, x + c0, z) < x + c1, y || (z < x + c1), c0 < c1, "lt218")) ||
              (rewrite(select(y, z, x + c0) < x + c1, y && (z < x + c1), c0 >= c1, "lt219")) ||
              (rewrite(select(y, z, x + c0) < x + c1, !y || (z < x + c1), c0 < c1, "lt220")) ||

              // Normalize comparison of ramps to a comparison of a ramp and a broadacst
              (rewrite(ramp(x, y) < ramp(z, w), ramp(x - z, y - w, lanes) < 0, "lt223")))) ||

            (no_overflow_int(ty) && EVAL_IN_LAMBDA
             ((rewrite(x * c0 < y * c1, x < y * fold(c1 / c0), c1 % c0 == 0 && c0 > 0, "lt226")) ||
              (rewrite(x * c0 < y * c1, x * fold(c0 / c1) < y, c0 % c1 == 0 && c1 > 0, "lt227")) ||

              (rewrite(x * c0 < y * c0 + c1, x < y + fold((c1 + c0 - 1)/c0), c0 > 0, "lt229")) ||
              (rewrite(x * c0 + c1 < y * c0, x + fold(c1/c0) < y, c0 > 0, "lt230")) ||

              // Comparison of stair-step functions. The basic transformation is:
              //   ((x + y)/c1)*c1 < x
              // = (x + y) - (x + y) % c1 < x (when c1 > 0)
              // = y - (x + y) % c1 < 0
              // = y < (x + y) % c1
              // This cancels x but duplicates y, so we only do it when y is a constant.

              // A more general version with extra terms w and z
              (rewrite(((x + c0)/c1)*c1 + w < x + z, (w + c0) < ((x + c0) % c1) + z, c1 > 0, "lt240")) ||
              (rewrite(w + ((x + c0)/c1)*c1 < x + z, (w + c0) < ((x + c0) % c1) + z, c1 > 0, "lt241")) ||
              (rewrite(((x + c0)/c1)*c1 + w < z + x, (w + c0) < ((x + c0) % c1) + z, c1 > 0, "lt242")) ||
              (rewrite(w + ((x + c0)/c1)*c1 < z + x, (w + c0) < ((x + c0) % c1) + z, c1 > 0, "lt243")) ||
              (rewrite(x + z < ((x + c0)/c1)*c1 + w, ((x + c0) % c1) + z < w + c0, c1 > 0, "lt244")) ||
              (rewrite(x + z < w + ((x + c0)/c1)*c1, ((x + c0) % c1) + z < w + c0, c1 > 0, "lt245")) ||
              (rewrite(z + x < ((x + c0)/c1)*c1 + w, ((x + c0) % c1) + z < w + c0, c1 > 0, "lt246")) ||
              (rewrite(z + x < w + ((x + c0)/c1)*c1, ((x + c0) % c1) + z < w + c0, c1 > 0, "lt247")) ||

              // w = 0
              (rewrite(((x + c0)/c1)*c1 < x + z, c0 < ((x + c0) % c1) + z, c1 > 0, "lt250")) ||
              (rewrite(((x + c0)/c1)*c1 < z + x, c0 < ((x + c0) % c1) + z, c1 > 0, "lt251")) ||
              (rewrite(x + z < ((x + c0)/c1)*c1, ((x + c0) % c1) + z < c0, c1 > 0, "lt252")) ||
              (rewrite(z + x < ((x + c0)/c1)*c1, ((x + c0) % c1) + z < c0, c1 > 0, "lt253")) ||

              // z = 0
              (rewrite(((x + c0)/c1)*c1 + w < x, (w + c0) < ((x + c0) % c1), c1 > 0, "lt256")) ||
              (rewrite(w + ((x + c0)/c1)*c1 < x, (w + c0) < ((x + c0) % c1), c1 > 0, "lt257")) ||
              (rewrite(x < ((x + c0)/c1)*c1 + w, ((x + c0) % c1) < w + c0, c1 > 0, "lt258")) ||
              (rewrite(x < w + ((x + c0)/c1)*c1, ((x + c0) % c1) < w + c0, c1 > 0, "lt259")) ||

              // c0 = 0
              (rewrite((x/c1)*c1 + w < x + z, w < (x % c1) + z, c1 > 0, "lt262")) ||
              (rewrite(w + (x/c1)*c1 < x + z, w < (x % c1) + z, c1 > 0, "lt263")) ||
              (rewrite((x/c1)*c1 + w < z + x, w < (x % c1) + z, c1 > 0, "lt264")) ||
              (rewrite(w + (x/c1)*c1 < z + x, w < (x % c1) + z, c1 > 0, "lt265")) ||
              (rewrite(x + z < (x/c1)*c1 + w, (x % c1) + z < w, c1 > 0, "lt266")) ||
              (rewrite(x + z < w + (x/c1)*c1, (x % c1) + z < w, c1 > 0, "lt267")) ||
              (rewrite(z + x < (x/c1)*c1 + w, (x % c1) + z < w, c1 > 0, "lt268")) ||
              (rewrite(z + x < w + (x/c1)*c1, (x % c1) + z < w, c1 > 0, "lt269")) ||

              // w = 0, z = 0
              (rewrite(((x + c0)/c1)*c1 < x, c0 < ((x + c0) % c1), c1 > 0, "lt272")) ||
              (rewrite(x < ((x + c0)/c1)*c1, ((x + c0) % c1) < c0, c1 > 0, "lt273")) ||

              // w = 0, c0 = 0
              (rewrite((x/c1)*c1 < x + z, 0 < (x % c1) + z, c1 > 0, "lt276")) ||
              (rewrite((x/c1)*c1 < z + x, 0 < (x % c1) + z, c1 > 0, "lt277")) ||
              (rewrite(x + z < (x/c1)*c1, (x % c1) + z < 0, c1 > 0, "lt278")) ||
              (rewrite(z + x < (x/c1)*c1, (x % c1) + z < 0, c1 > 0, "lt279")) ||

              // z = 0, c0 = 0
              (rewrite((x/c1)*c1 + w < x, w < (x % c1), c1 > 0, "lt282")) ||
              (rewrite(w + (x/c1)*c1 < x, w < (x % c1), c1 > 0, "lt283")) ||
              (rewrite(x < (x/c1)*c1 + w, (x % c1) < w, c1 > 0, "lt284")) ||
              (rewrite(x < w + (x/c1)*c1, (x % c1) < w, c1 > 0, "lt285")) ||

              // z = 0, c0 = 0, w = 0
              (rewrite((x/c1)*c1 < x, (x % c1) != 0, c1 > 0, "lt288")) ||
              (rewrite(x < (x/c1)*c1, false, c1 > 0, "lt289")) ||

              // Cancel a division
              (rewrite((x + c1)/c0 < (x + c2)/c0, false, c0 > 0 && c1 >= c2, "lt292")) ||
              (rewrite((x + c1)/c0 < (x + c2)/c0, true, c0 > 0 && c1 <= c2 - c0, "lt293")) ||
              // c1 == 0
              (rewrite(x/c0 < (x + c2)/c0, false, c0 > 0 && 0 >= c2, "lt295")) ||
              (rewrite(x/c0 < (x + c2)/c0, true, c0 > 0 && 0 <= c2 - c0, "lt296")) ||
              // c2 == 0
              (rewrite((x + c1)/c0 < x/c0, false, c0 > 0 && c1 >= 0, "lt298")) ||
              (rewrite((x + c1)/c0 < x/c0, true, c0 > 0 && c1 <= 0 - c0, "lt299")) ||

              // The addition on the right could be outside
              (rewrite((x + c1)/c0 < x/c0 + c2, false, c0 > 0 && c1 >= c2 * c0, "lt302")) ||
              (rewrite((x + c1)/c0 < x/c0 + c2, true, c0 > 0 && c1 <= c2 * c0 - c0, "lt303")) ||

              // With a confounding max or min
              (rewrite((x + c1)/c0 < (min(x/c0, y) + c2), false, c0 > 0 && c1 >= c2 * c0, "lt306")) ||
              (rewrite((x + c1)/c0 < (max(x/c0, y) + c2), true, c0 > 0 && c1 <= c2 * c0 - c0, "lt307")) ||
              (rewrite((x + c1)/c0 < min((x + c2)/c0, y), false, c0 > 0 && c1 >= c2, "lt308")) ||
              (rewrite((x + c1)/c0 < max((x + c2)/c0, y), true, c0 > 0 && c1 <= c2 - c0, "lt309")) ||
              (rewrite((x + c1)/c0 < min(x/c0, y), false, c0 > 0 && c1 >= 0, "lt310")) ||
              (rewrite((x + c1)/c0 < max(x/c0, y), true, c0 > 0 && c1 <= 0 - c0, "lt311")) ||

              (rewrite((x + c1)/c0 < (min(y, x/c0) + c2), false, c0 > 0 && c1 >= c2 * c0, "lt313")) ||
              (rewrite((x + c1)/c0 < (max(y, x/c0) + c2), true, c0 > 0 && c1 <= c2 * c0 - c0, "lt314")) ||
              (rewrite((x + c1)/c0 < min(y, (x + c2)/c0), false, c0 > 0 && c1 >= c2, "lt315")) ||
              (rewrite((x + c1)/c0 < max(y, (x + c2)/c0), true, c0 > 0 && c1 <= c2 - c0, "lt316")) ||
              (rewrite((x + c1)/c0 < min(y, x/c0), false, c0 > 0 && c1 >= 0, "lt317")) ||
              (rewrite((x + c1)/c0 < max(y, x/c0), true, c0 > 0 && c1 <= 0 - c0, "lt318")) ||

              (rewrite(max((x + c2)/c0, y) < (x + c1)/c0, false, c0 > 0 && c2 >= c1, "lt320")) ||
              (rewrite(min((x + c2)/c0, y) < (x + c1)/c0, true, c0 > 0 && c2 <= c1 - c0, "lt321")) ||
              (rewrite(max(x/c0, y) < (x + c1)/c0, false, c0 > 0 && 0 >= c1, "lt322")) ||
              (rewrite(min(x/c0, y) < (x + c1)/c0, true, c0 > 0 && 0 <= c1 - c0, "lt323")) ||
              (rewrite(max(y, (x + c2)/c0) < (x + c1)/c0, false, c0 > 0 && c2 >= c1, "lt324")) ||
              (rewrite(min(y, (x + c2)/c0) < (x + c1)/c0, true, c0 > 0 && c2 <= c1 - c0, "lt325")) ||
              (rewrite(max(y, x/c0) < (x + c1)/c0, false, c0 > 0 && 0 >= c1, "lt326")) ||
              (rewrite(min(y, x/c0) < (x + c1)/c0, true, c0 > 0 && 0 <= c1 - c0, "lt327")) ||

              // Same as above with c1 outside the division, with redundant cases removed.
              (rewrite(max((x + c2)/c0, y) < x/c0 + c1, false, c0 > 0 && c2 >= c1 * c0, "lt330")) ||
              (rewrite(min((x + c2)/c0, y) < x/c0 + c1, true, c0 > 0 && c2 <= c1 * c0 - c0, "lt331")) ||
              (rewrite(max(y, (x + c2)/c0) < x/c0 + c1, false, c0 > 0 && c2 >= c1 * c0, "lt332")) ||
              (rewrite(min(y, (x + c2)/c0) < x/c0 + c1, true, c0 > 0 && c2 <= c1 * c0 - c0, "lt333")) ||

              // Same as above with c1 = 0 and the predicates and redundant cases simplified accordingly.
              (rewrite(x/c0 < min((x + c2)/c0, y), false, c0 > 0 && c2 < 0, "lt336")) ||
              (rewrite(x/c0 < max((x + c2)/c0, y), true, c0 > 0 && c0 <= c2, "lt337")) ||
              (rewrite(x/c0 < min(y, (x + c2)/c0), false, c0 > 0 && c2 < 0, "lt338")) ||
              (rewrite(x/c0 < max(y, (x + c2)/c0), true, c0 > 0 && c0 <= c2, "lt339")) ||
              (rewrite(max((x + c2)/c0, y) < x/c0, false, c0 > 0 && c2 >= 0, "lt340")) ||
              (rewrite(min((x + c2)/c0, y) < x/c0, true, c0 > 0 && c2 + c0 <= 0, "lt341")) ||
              (rewrite(max(y, (x + c2)/c0) < x/c0, false, c0 > 0 && c2 >= 0, "lt342")) ||
              (rewrite(min(y, (x + c2)/c0) < x/c0, true, c0 > 0 && c2 + c0 <= 0, "lt343")) ||

              // Comparison of two mins/maxes that don't cancel when subtracted
              (rewrite(min(x, c0) < min(x, c1), false, c0 >= c1, "lt346")) ||
              (rewrite(min(x, c0) < min(x, c1) + c2, false, c0 >= c1 + c2 && c2 <= 0, "lt347")) ||
              (rewrite(max(x, c0) < max(x, c1), false, c0 >= c1, "lt348")) ||
              (rewrite(max(x, c0) < max(x, c1) + c2, false, c0 >= c1 + c2 && c2 <= 0, "lt349")) ||

              // Comparison of aligned ramps can simplify to a comparison of the base
              (rewrite(ramp(x * c3 + c2, c1) < broadcast(z * c0),
                      broadcast(x * fold(c3/c0) + fold(c2/c0) < z, lanes),
                      c0 > 0 && (c3 % c0 == 0) &&
                      (c2 % c0) + c1 * (lanes - 1) < c0 &&
                      (c2 % c0) + c1 * (lanes - 1) >= 0, "lt352")) ||
              // c2 = 0
              (rewrite(ramp(x * c3, c1) < broadcast(z * c0),
                      broadcast(x * fold(c3/c0) < z, lanes),
                      c0 > 0 && (c3 % c0 == 0) &&
                      c1 * (lanes - 1) < c0 &&
                      c1 * (lanes - 1) >= 0, "lt358"))))) {
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
