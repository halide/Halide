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
        bounds->min = a_bounds.min + b_bounds.min;
        bounds->max = a_bounds.max + b_bounds.max;
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

        if (rewrite(c0 + c1, fold(c0 + c1)) ||
            rewrite(IRMatcher::Indeterminate() + x, a) ||
            rewrite(x + IRMatcher::Indeterminate(), b) ||
            rewrite(IRMatcher::Overflow() + x, a) ||
            rewrite(x + IRMatcher::Overflow(), b) ||
            rewrite(x + 0, x) ||
            rewrite(0 + x, x)) {
            return rewrite.result;
        }

        if (EVAL_IN_LAMBDA
            (rewrite(x + x, x * 2) ||
             rewrite(ramp(x, y) + ramp(z, w), ramp(x + z, y + w, lanes)) ||
             rewrite(ramp(x, y) + broadcast(z), ramp(x + z, y, lanes)) ||
             rewrite(broadcast(x) + broadcast(y), broadcast(x + y, lanes)) ||
             rewrite(select(x, y, z) + select(x, w, u), select(x, y + w, z + u)) ||
             rewrite(select(x, c0, c1) + c2, select(x, fold(c0 + c2), fold(c1 + c2))) ||
             //             rewrite(select(x, y, c1) + c2, select(x, y + c2, fold(c1 + c2))) ||
             rewrite(select(x, c0, y) + c2, select(x, fold(c0 + c2), y + c2)) ||

             rewrite((select(x, y, z) + w) + select(x, u, v), select(x, y + u, z + v) + w) ||
             rewrite((w + select(x, y, z)) + select(x, u, v), select(x, y + u, z + v) + w) ||
             rewrite(select(x, y, z) + (select(x, u, v) + w), select(x, y + u, z + v) + w) ||
             rewrite(select(x, y, z) + (w + select(x, u, v)), select(x, y + u, z + v) + w) ||
             rewrite((select(x, y, z) - w) + select(x, u, v), select(x, y + u, z + v) - w) ||
             rewrite(select(x, y, z) + (select(x, u, v) - w), select(x, y + u, z + v) - w) ||
             rewrite((w - select(x, y, z)) + select(x, u, v), select(x, u - y, v - z) + w) ||
             rewrite(select(x, y, z) + (w - select(x, u, v)), select(x, y - u, z - v) + w) ||

             rewrite((x + c0) + c1, x + fold(c0 + c1)) ||
             rewrite((x + c0) + y, (x + y) + c0) ||
             rewrite(x + (y + c0), (x + y) + c0) ||
             rewrite((c0 - x) + c1, fold(c0 + c1) - x) ||
             rewrite((c0 - x) + y, (y - x) + c0) ||
             rewrite((x - y) + y, x) ||
             rewrite(x + (y - x), y) ||
             rewrite(x + (c0 - y), (x - y) + c0) ||
             rewrite((x - y) + (y - z), x - z) ||
             rewrite((x - y) + (z - x), z - y) ||
             rewrite(x + y*c0, x - y*(-c0), c0 < 0 && -c0 > 0) ||
             rewrite(x*c0 + y, y - x*(-c0), c0 < 0 && -c0 > 0 && !is_const(y)) ||
             rewrite(x*y + z*y, (x + z)*y) ||
             rewrite(x*y + y*z, (x + z)*y) ||
             rewrite(y*x + z*y, y*(x + z)) ||
             rewrite(y*x + y*z, y*(x + z)) ||
             rewrite(x*c0 + y*c1, (x + y*fold(c1/c0)) * c0, c1 % c0 == 0) ||
             rewrite(x*c0 + y*c1, (x*fold(c0/c1) + y) * c1, c0 % c1 == 0) ||
             (no_overflow(op->type) &&
              (rewrite(x + x*y, x * (y + 1)) ||
               rewrite(x + y*x, (y + 1) * x) ||
               rewrite(x*y + x, x * (y + 1)) ||
               rewrite(y*x + x, (y + 1) * x, !is_const(x)) ||
               rewrite((x + c0)/c1 + c2, (x + fold(c0 + c1*c2))/c1) ||
               rewrite((x + (y + c0)/c1) + c2, x + (y + fold(c0 + c1*c2))/c1) ||
               rewrite(((y + c0)/c1 + x) + c2, x + (y + fold(c0 + c1*c2))/c1) ||
               rewrite((c0 - x)/c1 + c2, (fold(c0 + c1*c2) - x)/c1, c0 != 0 && c1 != 0) || // When c0 is zero, this would fight another rule
               rewrite(x + (x + y)/c0, (fold(c0 + 1)*x + y)/c0) ||
               rewrite(x + (y + x)/c0, (fold(c0 + 1)*x + y)/c0) ||
               rewrite(x + (y - x)/c0, (fold(c0 - 1)*x + y)/c0) ||
               rewrite(x + (x - y)/c0, (fold(c0 + 1)*x - y)/c0) ||
               rewrite((x - y)/c0 + x, (fold(c0 + 1)*x - y)/c0) ||
               rewrite((y - x)/c0 + x, (y + fold(c0 - 1)*x)/c0) ||
               rewrite((x + y)/c0 + x, (fold(c0 + 1)*x + y)/c0) ||
               rewrite((y + x)/c0 + x, (y + fold(c0 + 1)*x)/c0) ||
               rewrite(min(x, y - z) + z, min(x + z, y)) ||
               rewrite(min(y - z, x) + z, min(y, x + z)) ||
               rewrite(min(x, y + c0) + c1, min(x + c1, y), c0 + c1 == 0) ||
               rewrite(min(y + c0, x) + c1, min(y, x + c1), c0 + c1 == 0) ||
               rewrite(z + min(x, y - z), min(z + x, y)) ||
               rewrite(z + min(y - z, x), min(y, z + x)) ||
               rewrite(z + max(x, y - z), max(z + x, y)) ||
               rewrite(z + max(y - z, x), max(y, z + x)) ||
               rewrite(max(x, y - z) + z, max(x + z, y)) ||
               rewrite(max(y - z, x) + z, max(y, x + z)) ||
               rewrite(max(x, y + c0) + c1, max(x + c1, y), c0 + c1 == 0) ||
               rewrite(max(y + c0, x) + c1, max(y, x + c1), c0 + c1 == 0) ||
               rewrite(max(x, y) + min(x, y), x + y) ||
               rewrite(max(x, y) + min(y, x), x + y))) ||
             (no_overflow_int(op->type) &&
              (rewrite((x/y)*y + x%y, x) ||
               rewrite((z + x/y)*y + x%y, z*y + x) ||
               rewrite((x/y + z)*y + x%y, x + z*y) ||
               rewrite(x%y + ((x/y)*y + z), x + z) ||
               rewrite(x%y + ((x/y)*y - z), x - z) ||
               rewrite(x%y + (z + (x/y)*y), x + z) ||
               rewrite((x/y)*y + (x%y + z), x + z) ||
               rewrite((x/y)*y + (x%y - z), x - z) ||
               rewrite((x/y)*y + (z + x%y), x + z) ||
               rewrite(x/2 + x%2, (x + 1) / 2) ||

               rewrite(x + ((c0 - x)/c1)*c1, c0 - ((c0 - x) % c1), c1 > 0) ||
               rewrite(x + ((c0 - x)/c1 + y)*c1, y * c1 - ((c0 - x) % c1) + c0, c1 > 0) ||
               rewrite(x + (y + (c0 - x)/c1)*c1, y * c1 - ((c0 - x) % c1) + c0, c1 > 0) ||

#if USE_SYNTHESIZED_RULES_V2
               rewrite((u + (x - (y + (z + (u + w))))), ((x - (w + z)) - y)) ||
 rewrite((u + (x - (y + (z + (w + u))))), ((x - (w + z)) - y)) ||
 rewrite((u + (x - (y + ((u + w) + z)))), ((x - (w + z)) - y)) ||
 rewrite((u + (x - (y + ((w + u) + z)))), ((x - (w + z)) - y)) ||
 rewrite((u + (x - ((z + (u + w)) + y))), ((x - (w + z)) - y)) ||
 rewrite((u + (x - ((z + (w + u)) + y))), ((x - (w + z)) - y)) ||
 rewrite((u + (x - (((u + w) + z) + y))), ((x - (w + z)) - y)) ||
 rewrite((u + (x - (((w + u) + z) + y))), ((x - (w + z)) - y)) ||
 rewrite((x + (w + (y - (x + z)))), ((w - z) + y)) ||
 rewrite((x + (w + (y - (z + x)))), ((w - z) + y)) ||
 rewrite((x + ((y - (x + z)) + w)), ((w - z) + y)) ||
 rewrite((x + ((y - (z + x)) + w)), ((w - z) + y)) ||
 rewrite((x + min(y, (c0 - max(x, c1)))), min((min(y, (c0 - c1)) + x), c0)) ||
 rewrite((x + min((c0 - max(x, c1)), y)), min((min(y, (c0 - c1)) + x), c0)) ||
 rewrite((y + (w + min(z, (x - y)))), (min((y + z), x) + w)) ||
 rewrite((y + (w + min((x - y), z))), (min((y + z), x) + w)) ||
 rewrite((y + (min(z, (x - y)) + w)), (min((y + z), x) + w)) ||
 rewrite((y + (min((x - y), z) + w)), (min((y + z), x) + w)) ||
 rewrite((y + (min(((x - y)*c0), c1)/c0)), min((y + 0), x), (((-1 <= (c0 + c1)) && (0 <= c1)) && ((max(c1, 0) + 1) <= c0))) ||
 rewrite((y + min(w, ((x - y) - z))), min((x - z), (w + y))) ||
 rewrite((y + min(((x - y) - z), w)), min((x - z), (w + y))) ||
 rewrite((y + max(w, (z + (x - y)))), max((x + z), (w + y))) ||
 rewrite((y + max(w, ((x - y) + z))), max((x + z), (w + y))) ||
 rewrite((y + max((z + (x - y)), w)), max((x + z), (w + y))) ||
 rewrite((y + max(((x - y) + z), w)), max((x + z), (w + y))) ||
 rewrite((z + (z*min(y, (x + -1)))), (min((y + 1), x)*z)) ||
 rewrite((z + (z*min((x + -1), y))), (min((y + 1), x)*z)) ||
 rewrite((z + (min(y, (x + -1))*z)), (min((y + 1), x)*z)) ||
 rewrite((z + (min((x + -1), y)*z)), (min((y + 1), x)*z)) ||
 rewrite((z + min(w, min(x, ((y - z) + c0)))), min((y + c0), (min(w, x) + z))) ||
 rewrite((z + min(w, min(((y - z) + c0), x))), min((y + c0), (min(w, x) + z))) ||
 rewrite((z + min(min(x, ((y - z) + c0)), w)), min((y + c0), (min(w, x) + z))) ||
 rewrite((z + min(min(((y - z) + c0), x), w)), min((y + c0), (min(w, x) + z))) ||
 rewrite((z + max(x, (y - (w + z)))), max((y - w), (x + z))) ||
 rewrite((z + max(x, (y - (z + w)))), max((y - w), (x + z))) ||
 rewrite((z + max((y - (w + z)), x)), max((y - w), (x + z))) ||
 rewrite((z + max((y - (z + w)), x)), max((y - w), (x + z))) ||
 rewrite(((w + (x - (y + z))) + (u + z)), ((u - y) + (w + x))) ||
 rewrite(((w + (x - (y + z))) + (z + u)), ((u - y) + (w + x))) ||
 rewrite(((w + (x - (z + y))) + (u + z)), ((u - y) + (w + x))) ||
 rewrite(((w + (x - (z + y))) + (z + u)), ((u - y) + (w + x))) ||
 rewrite(((w + (y - (x + z))) + x), ((w - z) + y)) ||
 rewrite(((w + (y - (z + x))) + x), ((w - z) + y)) ||
 rewrite(((w + min(z, (x - y))) + y), (min((y + z), x) + w)) ||
 rewrite(((w + min((x - y), z)) + y), (min((y + z), x) + w)) ||
 rewrite((((x - (y + z)) + w) + (u + z)), ((u - y) + (w + x))) ||
 rewrite((((x - (y + z)) + w) + (z + u)), ((u - y) + (w + x))) ||
 rewrite((((x - (z + y)) + w) + (u + z)), ((u - y) + (w + x))) ||
 rewrite((((x - (z + y)) + w) + (z + u)), ((u - y) + (w + x))) ||
 rewrite((((y - (x + z)) + w) + x), ((w - z) + y)) ||
 rewrite((((y - (z + x)) + w) + x), ((w - z) + y)) ||
 rewrite(((((x + c0)*y) + (x*c1)) + c2), ((y + c1)*(x + c0)), (((((c0 == 0) || (c2 == 0)) || (1 <= c1)) || (c1 <= -1)) && ((((1 <= c0) || (c2 == 0)) || (c0 <= -1)) && ((c0*c1) == c2)))) ||
 rewrite(((min(z, (x - y)) + w) + y), (min((y + z), x) + w)) ||
 rewrite(((min((x - y), z) + w) + y), (min((y + z), x) + w)) ||
 rewrite(((x - y) + (z + (w + y))), ((x + z) + w)) ||
 rewrite(((x - y) + (z + (y + w))), ((x + z) + w)) ||
 rewrite(((x - y) + ((w + y) + z)), ((x + z) + w)) ||
 rewrite(((x - y) + ((y + w) + z)), ((x + z) + w)) ||
 rewrite(((x - (u + (y + (w + z)))) + (w + z)), (x - (u + y))) ||
 rewrite(((x - (u + (y + (w + z)))) + (z + w)), (x - (u + y))) ||
 rewrite(((x - (u + (y + (z + w)))) + (w + z)), (x - (u + y))) ||
 rewrite(((x - (u + (y + (z + w)))) + (z + w)), (x - (u + y))) ||
 rewrite(((x - (u + ((w + z) + y))) + (w + z)), (x - (u + y))) ||
 rewrite(((x - (u + ((w + z) + y))) + (z + w)), (x - (u + y))) ||
 rewrite(((x - (u + ((z + w) + y))) + (w + z)), (x - (u + y))) ||
 rewrite(((x - (u + ((z + w) + y))) + (z + w)), (x - (u + y))) ||
 rewrite(((x - (y + z)) + (w + z)), ((w - y) + x)) ||
 rewrite(((x - (y + z)) + (z + w)), ((w - y) + x)) ||
 rewrite(((x - (y + (z + (u + w)))) + u), ((x - (w + z)) - y)) ||
 rewrite(((x - (y + (z + (w + u)))) + u), ((x - (w + z)) - y)) ||
 rewrite(((x - (y + ((u + w) + z))) + u), ((x - (w + z)) - y)) ||
 rewrite(((x - (y + ((w + u) + z))) + u), ((x - (w + z)) - y)) ||
 rewrite(((x - (z + y)) + (w + z)), ((w - y) + x)) ||
 rewrite(((x - (z + y)) + (z + w)), ((w - y) + x)) ||
 rewrite(((x - ((y + (w + z)) + u)) + (w + z)), (x - (u + y))) ||
 rewrite(((x - ((y + (w + z)) + u)) + (z + w)), (x - (u + y))) ||
 rewrite(((x - ((y + (z + w)) + u)) + (w + z)), (x - (u + y))) ||
 rewrite(((x - ((y + (z + w)) + u)) + (z + w)), (x - (u + y))) ||
 rewrite(((x - ((z + (u + w)) + y)) + u), ((x - (w + z)) - y)) ||
 rewrite(((x - ((z + (w + u)) + y)) + u), ((x - (w + z)) - y)) ||
 rewrite(((x - (((u + w) + z) + y)) + u), ((x - (w + z)) - y)) ||
 rewrite(((x - (((w + u) + z) + y)) + u), ((x - (w + z)) - y)) ||
 rewrite(((x - (((w + z) + y) + u)) + (w + z)), (x - (u + y))) ||
 rewrite(((x - (((w + z) + y) + u)) + (z + w)), (x - (u + y))) ||
 rewrite(((x - (((z + w) + y) + u)) + (w + z)), (x - (u + y))) ||
 rewrite(((x - (((z + w) + y) + u)) + (z + w)), (x - (u + y))) ||
 rewrite((((x - (y + z)) - w) + (u + y)), ((u - (w + z)) + x)) ||
 rewrite((((x - (y + z)) - w) + (y + u)), ((u - (w + z)) + x)) ||
 rewrite((((x - (z + y)) - w) + (u + y)), ((u - (w + z)) + x)) ||
 rewrite((((x - (z + y)) - w) + (y + u)), ((u - (w + z)) + x)) ||
 rewrite(((x*c0) + (((y - (x*c1))*c2) - z)), ((y*c2) - z), ((c1*c2) == c0)) ||
 rewrite(((x*y) + (z - (w + (y*(u + x))))), (z - ((u*y) + w))) ||
 rewrite(((x*y) + (z - (w + (y*(x + u))))), (z - ((u*y) + w))) ||
 rewrite(((x*y) + (z - (w + ((u + x)*y)))), (z - ((u*y) + w))) ||
 rewrite(((x*y) + (z - (w + ((x + u)*y)))), (z - ((u*y) + w))) ||
 rewrite(((x*y) + (z - ((y*(u + x)) + w))), (z - ((u*y) + w))) ||
 rewrite(((x*y) + (z - ((y*(x + u)) + w))), (z - ((u*y) + w))) ||
 rewrite(((x*y) + (z - (((u + x)*y) + w))), (z - ((u*y) + w))) ||
 rewrite(((x*y) + (z - (((x + u)*y) + w))), (z - ((u*y) + w))) ||
 rewrite(((x*y) + (z - (w*y))), (((x - w)*y) + z)) ||
 rewrite(((x*y) + (z - (y*w))), (((x - w)*y) + z)) ||
 rewrite(((x*y) + ((y*z) - w)), (((x + z)*y) - w)) ||
 rewrite(((x*y) + ((z*y) - w)), (((x + z)*y) - w)) ||
 rewrite(((y*x) + (z - (w + (y*(u + x))))), (z - ((u*y) + w))) ||
 rewrite(((y*x) + (z - (w + (y*(x + u))))), (z - ((u*y) + w))) ||
 rewrite(((y*x) + (z - (w + ((u + x)*y)))), (z - ((u*y) + w))) ||
 rewrite(((y*x) + (z - (w + ((x + u)*y)))), (z - ((u*y) + w))) ||
 rewrite(((y*x) + (z - ((y*(u + x)) + w))), (z - ((u*y) + w))) ||
 rewrite(((y*x) + (z - ((y*(x + u)) + w))), (z - ((u*y) + w))) ||
 rewrite(((y*x) + (z - (((u + x)*y) + w))), (z - ((u*y) + w))) ||
 rewrite(((y*x) + (z - (((x + u)*y) + w))), (z - ((u*y) + w))) ||
 rewrite(((y*x) + (z - (w*y))), (((x - w)*y) + z)) ||
 rewrite(((y*x) + (z - (y*w))), (((x - w)*y) + z)) ||
 rewrite(((y*x) + ((y*z) - w)), (((x + z)*y) - w)) ||
 rewrite(((y*x) + ((z*y) - w)), (((x + z)*y) - w)) ||
 rewrite(((z*min(y, (x + -1))) + z), (min((y + 1), x)*z)) ||
 rewrite(((z*min((x + -1), y)) + z), (min((y + 1), x)*z)) ||
 rewrite((((x - (y*c0))*c1) + (z + (y*c2))), ((x*c1) + z), ((c0*c1) == c2)) ||
 rewrite((((x - (y*c0))*c1) + ((y*c2) + z)), ((x*c1) + z), ((c0*c1) == c2)) ||
 rewrite(((min(y, (x + -1))*z) + z), (min((y + 1), x)*z)) ||
 rewrite(((min((x + -1), y)*z) + z), (min((y + 1), x)*z)) ||
 rewrite(((min(((x - y)*c0), c1)/c0) + y), min((y + 0), x), (((-1 <= (c0 + c1)) && (0 <= c1)) && ((max(c1, 0) + 1) <= c0))) ||
 rewrite((min(w, ((x - y) - z)) + y), min((x - z), (w + y))) ||
 rewrite((min(w, min(x, ((y - z) + c0))) + z), min((y + c0), (min(w, x) + z))) ||
 rewrite((min(w, min(((y - z) + c0), x)) + z), min((y + c0), (min(w, x) + z))) ||
 rewrite((min(x, (y - z)) + (w + z)), (min((x + z), y) + w)) ||
 rewrite((min(x, (y - z)) + (z + w)), (min((x + z), y) + w)) ||
 rewrite((min(y, (c0 - max(x, c1))) + x), min((min(y, (c0 - c1)) + x), c0)) ||
 rewrite((min((min(min((x + c0), y), z) + c1), y) + c2), min(min(min(y, z), x), (min(y, z) + (max((min(c1, 0) + -1), c1) + c2))), (((c1 <= -1) || ((max(c0, 0) + c2) <= 0)) && (((-1 <= (c0 + c2)) && (((c0 + c1) + c2) == 0)) && ((max(c2, 0) + c1) <= 0)))) ||
 rewrite((min((min(min(min(x, y), z), c0) + c1), y) + c1), (min(min(min(x, z), y), c0) + (c1*2)), (c1 <= max((min(c1, 0)*2), -1))) ||
 rewrite((min((c0 - max(x, c1)), y) + x), min((min(y, (c0 - c1)) + x), c0)) ||
 rewrite((min((y - z), x) + (w + z)), (min((x + z), y) + w)) ||
 rewrite((min((y - z), x) + (z + w)), (min((x + z), y) + w)) ||
 rewrite((min(((x - y) - z), w) + y), min((x - z), (w + y))) ||
 rewrite((min(((x - y)*z), c0) + (y*z)), min((x*z), ((y*z) + c0))) ||
 rewrite((min(((x - y)*z), c0) + (z*y)), min((x*z), ((y*z) + c0))) ||
 rewrite((min(((x - (y*c0))*c0), c1) + (y*c2)), min((x*c0), ((y*c2) + c1)), ((1 <= c2) && ((c0*c0) == c2))) ||
 rewrite((min(((x - (y*c0))*c1), c2) + (y*c3)), min((x*c1), ((y*c3) + c2)), (((((((1 <= max(c1, c3)) || (c0 <= -1)) || (c2 <= 2)) || (c3 <= -1)) || ((c1 + c2) <= 0)) || ((c1 + c2) <= 1)) && (((((((0 <= c2) || (1 <= c0)) || (1 <= c1)) || (1 <= c3)) || (c0 <= -1)) || ((c1 + 1) <= c2)) && (((((1 <= max(c1, c3)) || (c0 <= -1)) || (c2 <= 1)) || (c3 <= -1)) && (((1 <= max(max(c0, c1), c3)) || (c2 <= 1)) && ((c0*c1) == c3)))))) ||
 rewrite((min(min(x, ((y - z) + c0)), w) + z), min((y + c0), (min(w, x) + z))) ||
 rewrite((min(min((x + c0), (y + z)), y) + c1), min(((min(z, 0) + y) + c1), x), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + c0), (y + z)), z) + c1), min(((min(y, 0) + z) + c1), x), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + y), (z + c0)), y) + c1), min(((min(x, 0) + y) + c1), z), ((c0 + c1) == 0)) ||
 rewrite((min(min(((y - z) + c0), x), w) + z), min((y + c0), (min(w, x) + z))) ||
 rewrite((min(select((x < y), c0, 0), select((x < z), c0, 0)) + c2), select((x < max(y, z)), 0, c2), (((c0 <= -1) || (c2 == 0)) && (((max(c2, 0) + c0) <= 0) && ((c0 + c2) == 0)))) ||
 rewrite((max(w, (z + (x - y))) + y), max((x + z), (w + y))) ||
 rewrite((max(w, ((x - y) + z)) + y), max((x + z), (w + y))) ||
 rewrite((max(x, c0) + max(min(x, c0), c1)), (max(x, c1) + c0), (c1 <= c0)) ||
 rewrite((max(x, y) + (z + min(x, y))), ((y + z) + x)) ||
 rewrite((max(x, y) + (z + min(y, x))), ((y + z) + x)) ||
 rewrite((max(x, y) + (min(x, y) + z)), ((y + z) + x)) ||
 rewrite((max(x, y) + (min(y, x) + z)), ((y + z) + x)) ||
 rewrite((max(x, (y - (w + z))) + z), max((y - w), (x + z))) ||
 rewrite((max(x, (y - (z + w))) + z), max((y - w), (x + z))) ||
 rewrite((max(y, x) + (z + min(x, y))), ((y + z) + x)) ||
 rewrite((max(y, x) + (z + min(y, x))), ((y + z) + x)) ||
 rewrite((max(y, x) + (min(x, y) + z)), ((y + z) + x)) ||
 rewrite((max(y, x) + (min(y, x) + z)), ((y + z) + x)) ||
 rewrite((max((z + (x - y)), w) + y), max((x + z), (w + y))) ||
 rewrite((max(((x - y) + z), w) + y), max((x + z), (w + y))) ||
 rewrite((max((y - (w + z)), x) + z), max((y - w), (x + z))) ||
 rewrite((max((y - (z + w)), x) + z), max((y - w), (x + z))) ||
 rewrite((max((min((x + c0), y) - min(x, c1)), c0) + c2), max(min((y + (c2 - c1)), x), c1), ((c0 + c2) == c1)) ||

#endif

               // Synthesized
               #if USE_SYNTHESIZED_RULES

               rewrite((((min(x, y)*(z + w)) + z) + w), ((min(x, y) + 1)*(w + z))) ||
               rewrite((((x + y)*z) + (w - (x*z))), ((y*z) + w)) ||
               rewrite((((x + y)*z) + (w - (y*z))), ((x*z) + w)) ||
               rewrite((((x - (y*c0))*c1) + ((y*c2) + z)), ((x*c1) + z), (c2 == (c0*c1))) ||
               rewrite((((x*y) + (z + (w*y))) + y), (z - (((-1 - x) - w)*y))) ||
               rewrite((((x*y)*z) + (w*y)), (((x*z) + w)*y)) ||
               rewrite((((x*y)*z) + (x*w)), (((y*z) + w)*x)) ||
               rewrite(((min((x + c0), y) + z) + c1), (min((y + c1), x) + z), ((c0 + c1) == 0)) ||
               rewrite(((min((x + c0), y)*c1) + c2), (min((y + fold((0 - c0))), x)*c1), (((c0*c1) + c2) == 0)) ||
               rewrite(((min((x - (y + z)), w) + y) + z), min(((y + z) + w), x)) ||
               rewrite(((min((x - (y + z)), w) + z) + u), (min((x - y), (z + w)) + u)) ||
               rewrite(((min((x - y), z) + (w + y)) + u), (min((y + z), x) + (w + u))) ||
               rewrite(((min((x - y), z) + (y + w)) + u), (min((y + z), x) + (w + u))) ||
               rewrite(((min((x - y), z) + w) + y), (min((y + z), x) + w)) ||
               rewrite(((min(min(x, (y + c1)), c1) + z) + c2), (min(min((x + c2), y), 0) + z), ((c1 + c2) == 0)) ||
               rewrite(((min(x, (y + c0)) + z) + c1), (min((x + c1), y) + z), ((c0 + c1) == 0)) ||
               rewrite(((min(x, (y + c0))*c1) + c2), (min((x + fold((0 - c0))), y)*c1), (((c0*c1) + c2) == 0)) ||
               rewrite(((x - (min((y + z), w) + u)) + z), ((x - u) - min((w - z), y))) ||
               rewrite(((x - (y + z)) + (w + z)), ((w - y) + x)) ||
               rewrite(((x - (y + z)) + z), (x - y)) ||
               rewrite(((x - (y*z)) + (w - (u*z))), ((w - ((u + y)*z)) + x)) ||
               rewrite(((x - max(y, ((z + w) + u))) + u), (x - max((y - u), (w + z)))) ||
               rewrite(((x - min((y + z), w)) + z), (x - min((w - z), y))) ||
               rewrite(((x - y) + (y + z)), (x + z)) ||
               rewrite(((x - y) + (z + y)), (x + z)) ||
               rewrite(((x*(y*z)) + (w*z)), (((x*y) + w)*z)) ||
               rewrite(((x*(y*z)) + (y*w)), (((x*z) + w)*y)) ||
               rewrite(((x*y) + ((y*z) + w)), (((x + z)*y) + w)) ||
               rewrite(((x*y) + ((z*y) + w)), (((x + z)*y) + w)) ||
               rewrite(((x*y) + ((z*y) - w)), (((x + z)*y) - w)) ||
               rewrite(((x*y) + (z - ((w + u)*y))), (z - (((u - x) + w)*y))) ||
               rewrite(((x*y) + (z - (y*w))), (((x - w)*y) + z)) ||
               rewrite((max(x, y) + (min(x, y) + z)), ((x + y) + z)) ||
               rewrite((min(((x - y) - z), w) + y), min((x - z), (w + y))) ||
               rewrite((min(((x - y)*z), w) + (y*z)), min((x*z), ((y*z) + w))) ||
               rewrite((min((x - (y + z)), c0) + (w + z)), (min((x - y), (z + c0)) + w)) ||
               rewrite((min((x - (y + z)), w) + (z + u)), (min((x - y), (z + w)) + u)) ||
               rewrite((min((x - (y + z)), w) + y), min((x - z), (y + w))) ||
               rewrite((min((x - (y + z)), w) + z), min((x - y), (z + w))) ||
               rewrite((min((x - y), z) + (w + y)), (min((y + z), x) + w)) ||
               rewrite((min((x - y), z) + (y + w)), (min((y + z), x) + w)) ||
               rewrite((min(min((x + c0), y), z) + c1), min((min(y, z) + c1), x), ((c0 + c1) == 0)) ||
               rewrite((min(min((x - y), z), w) + y), min((min(z, w) + y), x)) ||
               rewrite((min(min(x, ((y - z) + w)), u) + z), min((min(x, u) + z), (y + w))) ||
               rewrite((min(min(x, (y + c0)), z) + c1), min((min(x, z) + c1), y), ((c0 + c1) == 0)) ||
               rewrite((min(min(x, (y - z)), w) + z), min((min(x, w) + z), y)) ||
               rewrite((min(x, ((y - z) + w)) + z), min((y + w), (x + z))) ||
               rewrite((min(x, (y - z)) + (z + w)), (min((x + z), y) + w)) ||
               rewrite((min(x, y) + (max(min(x, z), y) + w)), (min(max(y, z), x) + (y + w))) ||
               rewrite((min(x, y) + min((min(x, y) + z), w)), (min((min(x, y) + z), w) + min(x, y))) ||
               rewrite((x + ((y*z) + (w + (u*z)))), (((u + y)*z) + (w + x))) ||
               rewrite((x + (y - (x + z))), (y - z)) ||
               rewrite((x + (y - (z + x))), (y - z)) ||
               #endif

               false)))) {
            return mutate(std::move(rewrite.result), bounds);
        }

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

}
}
