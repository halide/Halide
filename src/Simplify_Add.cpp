#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Add *op, ExprInfo *bounds) {
    ExprInfo a_bounds, b_bounds;
    Expr a = mutate(op->a, &a_bounds);
    Expr b = mutate(op->b, &b_bounds);

    if (bounds && no_overflow_int(op->type)) {
        bounds->min_defined = a_bounds.min_defined && b_bounds.min_defined;
        bounds->max_defined = a_bounds.max_defined && b_bounds.max_defined;
        if (add_would_overflow(64, a_bounds.min, b_bounds.min)) {
            bounds->min_defined = false;
            bounds->min = 0;
        } else {
            bounds->min = a_bounds.min + b_bounds.min;
        }
        if (add_would_overflow(64, a_bounds.max, b_bounds.max)) {
            bounds->max_defined = false;
            bounds->max = 0;
        } else {
            bounds->max = a_bounds.max + b_bounds.max;
        }

        bounds->alignment = a_bounds.alignment + b_bounds.alignment;
        bounds->trim_bounds_using_alignment();
    }

    if (may_simplify(op->type)) {

        // Order commutative operations by node type
        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_bounds, b_bounds);
        }

        auto rewrite = IRMatcher::rewriter(IRMatcher::add(a, b), op->type);
        const int lanes = op->type.lanes();

        if ((rewrite(c0 + c1, fold(c0 + c1), "add42")) ||
            (rewrite(IRMatcher::Overflow() + x, a, "add43")) ||
            (rewrite(x + IRMatcher::Overflow(), b, "add44")) ||
            (rewrite(x + 0, x, "add45")) ||
            (rewrite(0 + x, x, "add46"))) {
            return rewrite.result;
        }

        // clang-format off
        if (EVAL_IN_LAMBDA
            ((rewrite(x + x, x * 2, "add52")) ||
             (rewrite(ramp(x, y) + ramp(z, w), ramp(x + z, y + w, lanes), "add53")) ||
             (rewrite(ramp(x, y) + broadcast(z), ramp(x + z, y, lanes), "add54")) ||
             (rewrite(broadcast(x) + broadcast(y), broadcast(x + y, lanes), "add55")) ||
             (rewrite(select(x, y, z) + select(x, w, u), select(x, y + w, z + u), "add56")) ||
             (rewrite(select(x, c0, c1) + c2, select(x, fold(c0 + c2), fold(c1 + c2)), "add57")) ||

             (rewrite(select(x, y, z) + (select(x, u, v) + w), select(x, y + u, z + v) + w, "add63")) ||
             (rewrite(select(x, y, z) + (w + select(x, u, v)), select(x, y + u, z + v) + w, "add64")) ||
             (rewrite(select(x, y, z) + (select(x, u, v) - w), select(x, y + u, z + v) - w, "add66")) ||
             (rewrite(select(x, y, z) + (w - select(x, u, v)), select(x, y - u, z - v) + w, "add68")) ||

             (rewrite((x + c0) + c1, x + fold(c0 + c1), "add70")) ||
             (rewrite((x + c0) + y, (x + y) + c0, "add71")) ||
             (rewrite(x + (y + c0), (x + y) + c0, "add72")) ||
             (rewrite((c0 - x) + c1, fold(c0 + c1) - x, "add73")) ||
             (rewrite((c0 - x) + y, (y - x) + c0, "add74")) ||

             (rewrite((x - y) + y, x, "add76")) ||
             (rewrite(x + (y - x), y, "add77")) ||

             (rewrite(((x - y) + z) + y, x + z, "add79")) ||
             (rewrite((z + (x - y)) + y, z + x, "add80")) ||
             (rewrite(x + ((y - x) + z), y + z, "add81")) ||
             (rewrite(x + (z + (y - x)), z + y, "add82")) ||

             (rewrite(x + (c0 - y), (x - y) + c0, "add84")) ||
             (rewrite((x - y) + (y - z), x - z, "add85")) ||
             (rewrite((x - y) + (z - x), z - y, "add86")) ||

             (rewrite(x*y + z*y, (x + z)*y, "add93")) ||
             (rewrite(x*y + y*z, (x + z)*y, "add94")) ||
             (rewrite(y*x + z*y, y*(x + z), "add95")) ||
             (rewrite(y*x + y*z, y*(x + z), "add96")) ||
             (rewrite(x*c0 + y*c1, (x + y*fold(c1/c0)) * c0, c1 % c0 == 0, "add97")) ||
             (rewrite(x*c0 + y*c1, (x*fold(c0/c1) + y) * c1, c0 % c1 == 0, "add98")) ||
             (no_overflow(op->type) &&
              ((rewrite(x + x*y, x * (y + 1), "add100")) ||
               (rewrite(x + y*x, (y + 1) * x, "add101")) ||
               (rewrite(x*y + x, x * (y + 1), "add102")) ||
               (rewrite(y*x + x, (y + 1) * x, !is_const(x), "add103")) ||
               (rewrite((x + c0)/c1 + c2, (x + fold(c0 + c1*c2))/c1, c1 != 0, "add104")) ||
               (rewrite((x + (y + c0)/c1) + c2, x + (y + fold(c0 + c1*c2))/c1, c1 != 0, "add105")) ||
               (rewrite(((y + c0)/c1 + x) + c2, x + (y + fold(c0 + c1*c2))/c1, c1 != 0, "add106")) ||
               (rewrite((c0 - x)/c1 + c2, (fold(c0 + c1*c2) - x)/c1, c0 != 0 && c1 != 0, "add107")) || // When c0 is zero, this would fight another rule
               (rewrite(x + (x + y)/c0, (fold(c0 + 1)*x + y)/c0, c0 != 0, "add108")) ||
               (rewrite(x + (y + x)/c0, (fold(c0 + 1)*x + y)/c0, c0 != 0, "add109")) ||
               (rewrite(x + (y - x)/c0, (fold(c0 - 1)*x + y)/c0, c0 != 0, "add110")) ||
               (rewrite(x + (x - y)/c0, (fold(c0 + 1)*x - y)/c0, c0 != 0, "add111")) ||
               (rewrite((x - y)/c0 + x, (fold(c0 + 1)*x - y)/c0, c0 != 0, "add112")) ||
               (rewrite((y - x)/c0 + x, (y + fold(c0 - 1)*x)/c0, c0 != 0, "add113")) ||
               (rewrite((x + y)/c0 + x, (fold(c0 + 1)*x + y)/c0, c0 != 0, "add114")) ||
               (rewrite((y + x)/c0 + x, (y + fold(c0 + 1)*x)/c0, c0 != 0, "add115")) ||
               (rewrite(min(x, y - z) + z, min(x + z, y), "add116")) ||
               (rewrite(min(y - z, x) + z, min(y, x + z), "add117")) ||
               (rewrite(min(x, y + c0) + c1, min(x + c1, y), c0 + c1 == 0, "add118")) ||
               (rewrite(min(y + c0, x) + c1, min(y, x + c1), c0 + c1 == 0, "add119")) ||
               (rewrite(z + min(x, y - z), min(z + x, y), "add120")) ||
               (rewrite(z + min(y - z, x), min(y, z + x), "add121")) ||
               (rewrite(z + max(x, y - z), max(z + x, y), "add122")) ||
               (rewrite(z + max(y - z, x), max(y, z + x), "add123")) ||
               (rewrite(max(x, y - z) + z, max(x + z, y), "add124")) ||
               (rewrite(max(y - z, x) + z, max(y, x + z), "add125")) ||
               (rewrite(max(x, y + c0) + c1, max(x + c1, y), c0 + c1 == 0, "add126")) ||
               (rewrite(max(y + c0, x) + c1, max(y, x + c1), c0 + c1 == 0, "add127")) ||
               (rewrite(max(x, y) + min(x, y), x + y, "add128")) ||
               (rewrite(max(x, y) + min(y, x), x + y, "add129")))) ||
             (no_overflow_int(op->type) &&
              ((rewrite((x/c0)*c0 + x%c0, x, c0 != 0, "add131")) ||
               (rewrite((z + x/c0)*c0 + x%c0, z*c0 + x, c0 != 0, "add132")) ||
               (rewrite((x/c0 + z)*c0 + x%c0, x + z*c0, c0 != 0, "add133")) ||
               (rewrite(x%c0 + ((x/c0)*c0 + z), x + z, c0 != 0, "add134")) ||
               (rewrite(x%c0 + ((x/c0)*c0 - z), x - z, c0 != 0, "add135")) ||
               (rewrite(x%c0 + (z + (x/c0)*c0), x + z, c0 != 0, "add136")) ||
               (rewrite((x/c0)*c0 + (x%c0 + z), x + z, c0 != 0, "add137")) ||
               (rewrite((x/c0)*c0 + (x%c0 - z), x - z, c0 != 0, "add138")) ||
               (rewrite((x/c0)*c0 + (z + x%c0), x + z, c0 != 0, "add139")) ||
               (rewrite(x/2 + x%2, (x + 1) / 2, "add140")) ||

               (rewrite(x + ((c0 - x)/c1)*c1, c0 - ((c0 - x) % c1), c1 > 0, "add142")) ||
               (rewrite(x + ((c0 - x)/c1 + y)*c1, y * c1 - ((c0 - x) % c1) + c0, c1 > 0, "add143")) ||
               (rewrite(x + (y + (c0 - x)/c1)*c1, y * c1 - ((c0 - x) % c1) + c0, c1 > 0, "add144")) ||
               false)))) {
            return mutate(std::move(rewrite.result), bounds);
        }
        // clang-format on

        const Shuffle *shuffle_a = a.as<Shuffle>();
        const Shuffle *shuffle_b = b.as<Shuffle>();
        if (shuffle_a && shuffle_b &&
            shuffle_a->is_slice() &&
            shuffle_b->is_slice()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return hoist_slice_vector<Add>(op);
            } else {
                return hoist_slice_vector<Add>(Add::make(a, b));
            }
        }
    }

    if (a.same_as(op->a) && b.same_as(op->b)) {
        return op;
    } else {
        return Add::make(a, b);
    }
}

}  // namespace Internal
}  // namespace Halide
