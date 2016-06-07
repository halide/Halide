#include <iostream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdio.h>

#include "Simplify.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRPrinter.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Var.h"
#include "Debug.h"
#include "ModulusRemainder.h"
#include "Substitute.h"
#include "Bounds.h"
#include "Deinterleave.h"
#include "ExprUsesVar.h"

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::pair;
using std::make_pair;
using std::ostringstream;
using std::vector;

namespace {

// Things that we can constant fold: Immediates and broadcasts of immediates.
bool is_simple_const(Expr e) {
    if (e.as<IntImm>()) return true;
    if (e.as<UIntImm>()) return true;
    if (e.as<FloatImm>()) return true;
    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_simple_const(b->value);
    }
    return false;
}

// Returns true iff t is a scalar integral type where overflow is undefined
bool no_overflow_scalar_int(Type t) {
    return (t.is_scalar() && t.is_int() && t.bits() >= 32);
}

// Returns true iff t does not have a well defined overflow behavior.
bool no_overflow(Type t) {
    return t.is_float() || no_overflow_scalar_int(t.element_of());
}

// Make a poison value used when overflow is detected during constant
// folding.
Expr signed_integer_overflow_error(Type t) {
    // Mark each call with an atomic counter, so that the errors can't
    // cancel against each other.
    static std::atomic<int> counter;
    return Call::make(t, Call::signed_integer_overflow, {counter++}, Call::Intrinsic);
}

}

class Simplify : public IRMutator {
public:
    Simplify(bool r, const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai) :
        simplify_lets(r) {
        alignment_info.set_containing_scope(ai);

        // Only respect the constant bounds from the containing scope.
        for (Scope<Interval>::const_iterator iter = bi->cbegin(); iter != bi->cend(); ++iter) {
            int64_t i_min, i_max;
            if (const_int(iter.value().min, &i_min) &&
                const_int(iter.value().max, &i_max)) {
                bounds_info.push(iter.name(), make_pair(i_min, i_max));
            }
        }

    }

    // Uncomment to debug all Expr mutations.
    /*
    Expr mutate(Expr e) {
        static int indent = 0;
        std::string spaces;
        for (int i = 0; i < indent; i++) spaces += ' ';

        debug(1) << spaces << "Simplifying " << e << "\n";
        indent++;
        Expr new_e = IRMutator::mutate(e);
        indent--;
        if (!new_e.same_as(e)) {
            debug(1)
                << spaces << "Before: " << e << "\n"
                << spaces << "After:  " << new_e << "\n";
        }
        return new_e;
    }
    using IRMutator::mutate;
    */

private:
    bool simplify_lets;

    struct VarInfo {
        Expr replacement;
        int old_uses, new_uses;
    };

    Scope<VarInfo> var_info;
    Scope<pair<int64_t, int64_t>> bounds_info;
    Scope<ModulusRemainder> alignment_info;


    using IRMutator::visit;

    // Wrappers for as_const_foo that are more convenient to use in
    // the large chains of conditions in the visit methods
    // below. Unlike the versions in IROperator, these only match
    // scalars.
    bool const_float(Expr e, double *f) {
        if (e.type().is_vector()) {
            return false;
        } else if (const double *p = as_const_float(e)) {
            *f = *p;
            return true;
        } else {
            return false;
        }
    }

    bool const_int(Expr e, int64_t *i) {
        if (e.type().is_vector()) {
            return false;
        } else if (const int64_t *p = as_const_int(e)) {
            *i = *p;
            return true;
        } else {
            return false;
        }
    }

    bool const_uint(Expr e, uint64_t *u) {
        if (e.type().is_vector()) {
            return false;
        } else if (const uint64_t *p = as_const_uint(e)) {
            *u = *p;
            return true;
        } else {
            return false;
        }
    }

    // Similar to bounds_of_expr_in_scope, but gives up immediately if
    // anything isn't a constant. This stops rules from taking the
    // bounds of something then having to simplify it to see whether
    // it constant-folds. For some expressions the bounds of the
    // expression is at least as complex as the expression, so
    // recursively mutating the bounds causes havoc.
    bool const_int_bounds(Expr e, int64_t *min_val, int64_t *max_val) {
        if (!no_overflow_scalar_int(e.type())) {
            return false;
        }

        if (const int64_t *i = as_const_int(e)) {
            *min_val = *max_val = *i;
            return true;
        } else if (const Variable *v = e.as<Variable>()) {
            if (bounds_info.contains(v->name)) {
                pair<int64_t, int64_t> b = bounds_info.get(v->name);
                *min_val = b.first;
                *max_val = b.second;
                return true;
            }
        } else if (const Add *add = e.as<Add>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(add->a, &min_a, &max_a) &&
                const_int_bounds(add->b, &min_b, &max_b)) {
                *min_val = min_a + min_b;
                *max_val = max_a + max_b;
                return true;
            }
        } else if (const Sub *sub = e.as<Sub>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(sub->a, &min_a, &max_a) &&
                const_int_bounds(sub->b, &min_b, &max_b)) {
                *min_val = min_a - max_b;
                *max_val = max_a - min_b;
                return true;
            }
        } else if (const Max *max = e.as<Max>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(max->a, &min_a, &max_a) &&
                const_int_bounds(max->b, &min_b, &max_b)) {
                *min_val = std::max(min_a, min_b);
                *max_val = std::max(max_a, max_b);
                return true;
            }
        } else if (const Min *min = e.as<Min>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(min->a, &min_a, &max_a) &&
                const_int_bounds(min->b, &min_b, &max_b)) {
                *min_val = std::min(min_a, min_b);
                *max_val = std::min(max_a, max_b);
                return true;
            }
        } else if (const Mul *mul = e.as<Mul>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(mul->a, &min_a, &max_a) &&
                const_int_bounds(mul->b, &min_b, &max_b)) {
                int64_t
                    t0 = min_a*min_b,
                    t1 = min_a*max_b,
                    t2 = max_a*min_b,
                    t3 = max_a*max_b;
                *min_val = std::min(std::min(t0, t1), std::min(t2, t3));
                *max_val = std::max(std::max(t0, t1), std::max(t2, t3));
                return true;
            }
        } else if (const Select *sel = e.as<Select>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(sel->true_value, &min_a, &max_a) &&
                const_int_bounds(sel->false_value, &min_b, &max_b)) {
                *min_val = std::min(min_a, min_b);
                *max_val = std::max(max_a, max_b);
                return true;
            }
        } else if (const Mod *mod = e.as<Mod>()) {
            int64_t min_b, max_b;
            if (const_int_bounds(mod->b, &min_b, &max_b) &&
                (min_b > 0 || max_b < 0)) {
                *min_val = 0;
                *max_val = std::abs(max_b) - 1;
                return true;
            }
        } else if (const Div *div = e.as<Div>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(div->a, &min_a, &max_a) &&
                const_int_bounds(div->b, &min_b, &max_b) &&
                (min_b > 0 || max_b < 0)) {
                int64_t
                    t0 = div_imp(min_a, min_b),
                    t1 = div_imp(min_a, max_b),
                    t2 = div_imp(max_a, min_b),
                    t3 = div_imp(max_a, max_b);
                *min_val = std::min(std::min(t0, t1), std::min(t2, t3));
                *max_val = std::max(std::max(t0, t1), std::max(t2, t3));
                return true;
            }
        }
        return false;
    }


    // Check if an Expr is integer-division-rounding-up by the given
    // factor. If so, return the core expression.
    Expr is_round_up_div(Expr e, int64_t factor) {
        if (!no_overflow(e.type())) return Expr();
        const Div *div = e.as<Div>();
        if (!div) return Expr();
        if (!is_const(div->b, factor)) return Expr();
        const Add *add = div->a.as<Add>();
        if (!add) return Expr();
        if (!is_const(add->b, factor-1)) return Expr();
        return add->a;
    }

    // Check if an Expr is a rounding-up operation, and if so, return
    // the factor.
    Expr is_round_up(Expr e, int64_t *factor) {
        if (!no_overflow(e.type())) return Expr();
        const Mul *mul = e.as<Mul>();
        if (!mul) return Expr();
        if (!const_int(mul->b, factor)) return Expr();
        return is_round_up_div(mul->a, *factor);
    }

    void visit(const Cast *op) {
        Expr value = mutate(op->value);
        const Cast *cast = value.as<Cast>();
        const Broadcast *broadcast_value = value.as<Broadcast>();
        const Ramp *ramp_value = value.as<Ramp>();
        const Add *add = value.as<Add>();
        double f = 0.0;
        int64_t i = 0;
        uint64_t u = 0;
        if (value.type() == op->type) {
            expr = value;
        } else if (op->type.is_int() &&
                   const_float(value, &f)) {
            // float -> int
            expr = IntImm::make(op->type, (int64_t)f);
        } else if (op->type.is_uint() &&
                   const_float(value, &f)) {
            // float -> uint
            expr = UIntImm::make(op->type, (uint64_t)f);
        } else if (op->type.is_float() &&
                   const_float(value, &f)) {
            // float -> float
            expr = FloatImm::make(op->type, f);
        } else if (op->type.is_int() &&
                   const_int(value, &i)) {
            // int -> int
            expr = IntImm::make(op->type, i);
        } else if (op->type.is_uint() &&
                   const_int(value, &i)) {
            // int -> uint
            expr = UIntImm::make(op->type, (uint64_t)i);
        } else if (op->type.is_float() &&
                   const_int(value, &i)) {
            // int -> float
            expr = FloatImm::make(op->type, (double)i);
        } else if (op->type.is_int() &&
                   const_uint(value, &u)) {
            // uint -> int
            expr = IntImm::make(op->type, (int64_t)u);
        } else if (op->type.is_uint() &&
                   const_uint(value, &u)) {
            // uint -> uint
            expr = UIntImm::make(op->type, u);
        } else if (op->type.is_float() &&
                   const_uint(value, &u)) {
            // uint -> float
            expr = FloatImm::make(op->type, (double)u);
        } else if (cast &&
                   op->type.code() == cast->type.code() &&
                   op->type.bits() < cast->type.bits()) {
            // If this is a cast of a cast of the same type, where the
            // outer cast is narrower, the inner cast can be
            // eliminated.
            expr = mutate(Cast::make(op->type, cast->value));
        } else if (broadcast_value) {
            // cast(broadcast(x)) -> broadcast(cast(x))
            expr = mutate(Broadcast::make(Cast::make(op->type.element_of(), broadcast_value->value), broadcast_value->lanes));
        } else if (ramp_value &&
                   op->type.element_of() == Int(64) &&
                   op->value.type().element_of() == Int(32)) {
            // cast(ramp(a, b, w)) -> ramp(cast(a), cast(b), w)
            expr = mutate(Ramp::make(Cast::make(op->type.element_of(), ramp_value->base),
                                     Cast::make(op->type.element_of(), ramp_value->stride),
                                     ramp_value->lanes));
        } else if (add &&
                   op->type == Int(64) &&
                   op->value.type() == Int(32) &&
                   is_const(add->b)) {
            // In the interest of moving constants outwards so they
            // can cancel, pull the addition outside of the cast.
            expr = mutate(Cast::make(op->type, add->a) + add->b);
        } else if (value.same_as(op->value)) {
            expr = op;
        } else {
            expr = Cast::make(op->type, value);
        }
    }

    void visit(const Variable *op) {
        if (var_info.contains(op->name)) {
            VarInfo &info = var_info.ref(op->name);

            // if replacement is defined, we should substitute it in (unless
            // it's a var that has been hidden by a nested scope).
            if (info.replacement.defined()) {
                internal_assert(info.replacement.type() == op->type);
                expr = info.replacement;
                info.new_uses++;
            } else {
                // This expression was not something deemed
                // substitutable - no replacement is defined.
                expr = op;
                info.old_uses++;
            }
        } else {
            // We never encountered a let that defines this var. Must
            // be a uniform. Don't touch it.
            expr = op;
        }
    }

    void visit(const Add *op) {
        int64_t ia = 0, ib = 0, ic = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;

        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        // Rearrange a few patterns to cut down on the number of cases
        // to check later.
        if ((is_simple_const(a) && !is_simple_const(b)) ||
            (b.as<Min>() && !a.as<Min>()) ||
            (b.as<Max>() && !a.as<Max>())) {
            std::swap(a, b);
        }

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();

        const Div *div_a = a.as<Div>();

        const Div *div_a_a = mul_a ? mul_a->a.as<Div>() : nullptr;
        const Mod *mod_a = a.as<Mod>();
        const Mod *mod_b = b.as<Mod>();

        const Mul *mul_a_a = add_a ? add_a->a.as<Mul>(): nullptr;
        const Mod *mod_a_a = add_a ? add_a->a.as<Mod>(): nullptr;
        const Mul *mul_a_b = add_a ? add_a->b.as<Mul>(): nullptr;
        const Mod *mod_a_b = add_a ? add_a->b.as<Mod>(): nullptr;

        const Min *min_a = a.as<Min>();
        const Max *max_a = a.as<Max>();
        const Sub *sub_a_a = min_a ? min_a->a.as<Sub>() : nullptr;
        const Sub *sub_a_b = min_a ? min_a->b.as<Sub>() : nullptr;
        const Add *add_a_a = min_a ? min_a->a.as<Add>() : nullptr;
        const Add *add_a_b = min_a ? min_a->b.as<Add>() : nullptr;
        sub_a_a = max_a ? max_a->a.as<Sub>() : sub_a_a;
        sub_a_b = max_a ? max_a->b.as<Sub>() : sub_a_b;
        add_a_a = max_a ? max_a->a.as<Add>() : add_a_a;
        add_a_b = max_a ? max_a->b.as<Add>() : add_a_b;

        add_a_a = div_a ? div_a->a.as<Add>() : add_a_a;

        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();

        if (const_int(a, &ia) &&
            const_int(b, &ib)) {
            if (no_overflow(a.type()) &&
                add_would_overflow(a.type().bits(), ia, ib)) {
                expr = signed_integer_overflow_error(a.type());
            } else {
                expr = IntImm::make(a.type(), ia + ib);
            }
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            // const uint + const uint
            expr = UIntImm::make(a.type(), ua + ub);
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb)) {
            // const float + const float
            expr = FloatImm::make(a.type(), fa + fb);
        } else if (is_zero(b)) {
            expr = a;
        } else if (is_zero(a)) {
            expr = b;
        } else if (ramp_a &&
                   ramp_b) {
            // Ramp + Ramp
            expr = mutate(Ramp::make(ramp_a->base + ramp_b->base,
                                     ramp_a->stride + ramp_b->stride, ramp_a->lanes));
        } else if (ramp_a &&
                   broadcast_b) {
            // Ramp + Broadcast
            expr = mutate(Ramp::make(ramp_a->base + broadcast_b->value,
                                     ramp_a->stride, ramp_a->lanes));
        } else if (broadcast_a &&
                   ramp_b) {
            // Broadcast + Ramp
            expr = mutate(Ramp::make(broadcast_a->value + ramp_b->base,
                                     ramp_b->stride, ramp_b->lanes));
        } else if (broadcast_a &&
                   broadcast_b) {
            // Broadcast + Broadcast
            expr = Broadcast::make(mutate(broadcast_a->value + broadcast_b->value),
                                   broadcast_a->lanes);

        } else if (select_a &&
                   select_b &&
                   equal(select_a->condition, select_b->condition)) {
            // select(c, a, b) + select(c, d, e) -> select(c, a+d, b+e)
            expr = mutate(Select::make(select_a->condition,
                                       select_a->true_value + select_b->true_value,
                                       select_a->false_value + select_b->false_value));
        } else if (select_a &&
                   is_simple_const(b) &&
                   (is_simple_const(select_a->true_value) ||
                    is_simple_const(select_a->false_value))) {
            // select(c, c1, c2) + c3 -> select(c, c1+c3, c2+c3)
            expr = mutate(Select::make(select_a->condition,
                                       select_a->true_value + b,
                                       select_a->false_value + b));
        } else if (add_a &&
                   is_simple_const(add_a->b)) {
            // In ternary expressions, pull constants outside
            if (is_simple_const(b)) {
                expr = mutate(add_a->a + (add_a->b + b));
            } else {
                expr = mutate((add_a->a + b) + add_a->b);
            }
        } else if (add_b &&
                   is_simple_const(add_b->b)) {
            expr = mutate((a + add_b->a) + add_b->b);
        } else if (sub_a &&
                   is_simple_const(sub_a->a)) {
            if (is_simple_const(b)) {
                expr = mutate((sub_a->a + b) - sub_a->b);
            } else {
                expr = mutate((b - sub_a->b) + sub_a->a);
            }

        } else if (sub_a &&
                   equal(b, sub_a->b)) {
            // Additions that cancel an inner term
            // (a - b) + b
            expr = sub_a->a;
        } else if (sub_a &&
                   is_zero(sub_a->a)) {
            expr = mutate(b - sub_a->b);
        } else if (sub_b && equal(a, sub_b->b)) {
            // a + (b - a)
            expr = sub_b->a;
        } else if (sub_b &&
                   is_simple_const(sub_b->a)) {
            // a + (7 - b) -> (a - b) + 7
            expr = mutate((a - sub_b->b) + sub_b->a);
        } else if (sub_a &&
                   sub_b &&
                   equal(sub_a->b, sub_b->a)) {
            // (a - b) + (b - c) -> a - c
            expr = mutate(sub_a->a - sub_b->b);
        } else if (sub_a &&
                   sub_b &&
                   equal(sub_a->a, sub_b->b)) {
            // (a - b) + (c - a) -> c - b
            expr = mutate(sub_b->a - sub_a->b);
        } else if (mul_b &&
                   is_negative_negatable_const(mul_b->b)) {
            // a + b*-x -> a - b*x
            expr = mutate(a - mul_b->a * (-mul_b->b));
        } else if (mul_a &&
                   is_negative_negatable_const(mul_a->b)) {
            // a*-x + b -> b - a*x
            expr = mutate(b - mul_a->a * (-mul_a->b));
        } else if (mul_b &&
                   !is_const(a) &&
                   equal(a, mul_b->a) &&
                   no_overflow(op->type)) {
            // a + a*b -> a*(1 + b)
            expr = mutate(a * (make_one(op->type) + mul_b->b));
        } else if (mul_b &&
                   !is_const(a) &&
                   equal(a, mul_b->b) &&
                   no_overflow(op->type)) {
            // a + b*a -> (1 + b)*a
            expr = mutate((make_one(op->type) + mul_b->a) * a);
        } else if (mul_a &&
                   !is_const(b) &&
                   equal(mul_a->a, b) &&
                   no_overflow(op->type)) {
            // a*b + a -> a*(b + 1)
            expr = mutate(mul_a->a * (mul_a->b + make_one(op->type)));
        } else if (mul_a &&
                   !is_const(b) &&
                   equal(mul_a->b, b) &&
                   no_overflow(op->type)) {
            // a*b + b -> (a + 1)*b
            expr = mutate((mul_a->a + make_one(op->type)) * b);
        } else if (no_overflow(op->type) &&
                   min_a &&
                   sub_a_b &&
                   equal(sub_a_b->b, b)) {
            // min(a, b-c) + c -> min(a+c, b)
            expr = mutate(Min::make(Add::make(min_a->a, b), sub_a_b->a));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   sub_a_a &&
                   equal(sub_a_a->b, b)) {
            // min(a-c, b) + c -> min(a, b+c)
            expr = mutate(Min::make(sub_a_a->a, Add::make(min_a->b, b)));
        } else if (no_overflow(op->type) &&
                   max_a &&
                   sub_a_b &&
                   equal(sub_a_b->b, b)) {
            // max(a, b-c) + c -> max(a+c, b)
            expr = mutate(Max::make(Add::make(max_a->a, b), sub_a_b->a));
        } else if (no_overflow(op->type) &&
                   max_a &&
                   sub_a_a &&
                   equal(sub_a_a->b, b)) {
            // max(a-c, b) + c -> max(a, b+c)
            expr = mutate(Max::make(sub_a_a->a, Add::make(max_a->b, b)));

        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_b &&
                   const_int(add_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ia + ib == 0) {
            // min(a, b + (-2)) + 2 -> min(a + 2, b)
            expr = mutate(Min::make(Add::make(min_a->a, b), add_a_b->a));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_a &&
                   const_int(add_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia + ib == 0) {
            // min(a + (-2), b) + 2 -> min(a, b + 2)
            expr = mutate(Min::make(add_a_a->a, Add::make(min_a->b, b)));
        } else if (no_overflow(op->type) &&
                   max_a &&
                   add_a_b &&
                   const_int(add_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ia + ib == 0) {
            // max(a, b + (-2)) + 2 -> max(a + 2, b)
            expr = mutate(Max::make(Add::make(max_a->a, b), add_a_b->a));
        } else if (no_overflow(op->type) &&
                   max_a &&
                   add_a_a &&
                   const_int(add_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia + ib == 0) {
            // max(a + (-2), b) + 2 -> max(a, b + 2)
            expr = mutate(Max::make(add_a_a->a, Add::make(max_a->b, b)));
        } else if (no_overflow(op->type) &&
                   div_a &&
                   add_a_a &&
                   const_int(add_a_a->b, &ia) &&
                   const_int(div_a->b, &ib) && ib &&
                   const_int(b, &ic)) {
            // ((a + ia) / ib + ic) -> (a + (ia + ib*ic)) / ib
            expr = mutate((add_a_a->a + IntImm::make(op->type, ia + ib*ic)) / div_a->b);
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->a, mul_b->a)) {
            // Pull out common factors a*x + b*x
            expr = mutate(mul_a->a * (mul_a->b + mul_b->b));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->b, mul_b->a)) {
            expr = mutate(mul_a->b * (mul_a->a + mul_b->b));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->b, mul_b->b)) {
            expr = mutate(mul_a->b * (mul_a->a + mul_b->a));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->a, mul_b->b)) {
            expr = mutate(mul_a->a * (mul_a->b + mul_b->a));
        } else if (mod_a &&
                   mul_b &&
                   equal(mod_a->b, mul_b->b)) {
            // (x%3) + y*3 -> y*3 + x%3
            expr = mutate(b + a);
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   mod_b &&
                   div_a_a &&
                   equal(mul_a->b, div_a_a->b) &&
                   equal(mul_a->b, mod_b->b) &&
                   equal(div_a_a->a, mod_b->a)) {
            // (x/3)*3 + x%3 -> x
            expr = div_a_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_a &&
                   mod_b &&
                   equal(mul_a_a->b, mod_b->b) &&
                   (!mod_a_b || !equal(mod_a_b->b, mod_b->b))) {
            // ((x*3) + y) + z%3 -> (x*3 + z%3) + y
            expr = mutate((add_a->a + b) + add_a->b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mod_a_a &&
                   mul_b &&
                   equal(mod_a_a->b, mul_b->b) &&
                   (!mod_a_b || !equal(mod_a_b->b, mul_b->b))) {
            // ((x%3) + y) + z*3 -> (z*3 + x%3) + y
            expr = mutate((b + add_a->a) + add_a->b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_b &&
                   mod_b &&
                   equal(mul_a_b->b, mod_b->b) &&
                   (!mod_a_a || !equal(mod_a_a->b, mod_b->b))) {
            // (y + (x*3)) + z%3 -> y + (x*3 + z%3)
            expr = mutate(add_a->a + (add_a->b + b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mod_a_b &&
                   mul_b &&
                   equal(mod_a_b->b, mul_b->b) &&
                   (!mod_a_a || !equal(mod_a_a->b, mul_b->b))) {
            // (y + (x%3)) + z*3 -> y + (z*3 + x%3)
            expr = mutate(add_a->a + (b + add_a->b));
        } else if (mul_a && mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   ia % ib == 0) {
            // x*4 + y*2 -> (x*2 + y)*2
            Expr ratio = make_const(a.type(), div_imp(ia, ib));
            expr = mutate((mul_a->a * ratio + mul_b->a) * mul_b->b);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            // If we've made no changes, and can't find a rule to apply, return the operator unchanged.
            expr = op;
        } else {
            expr = Add::make(a, b);
        }
    }

    void visit(const Sub *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        int64_t ia = 0, ib = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();

        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Div *div_a_a = mul_a ? mul_a->a.as<Div>() : nullptr;
        const Div *div_b_a = mul_b ? mul_b->a.as<Div>() : nullptr;

        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();

        const Min *min_b = b.as<Min>();
        const Add *add_b_a = min_b ? min_b->a.as<Add>() : nullptr;
        const Add *add_b_b = min_b ? min_b->b.as<Add>() : nullptr;

        const Min *min_a = a.as<Min>();
        const Add *add_a_a = min_a ? min_a->a.as<Add>() : nullptr;
        const Add *add_a_b = min_a ? min_a->b.as<Add>() : nullptr;

        if (div_a) {
            add_a_a = div_a->a.as<Add>();
            add_a_b = div_a->b.as<Add>();
        }
        if (div_b) {
            add_b_a = div_b->a.as<Add>();
            add_b_b = div_b->b.as<Add>();
        }

        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();

        const Sub *sub_a_a = div_a ? div_a->a.as<Sub>() : nullptr;
        const Sub *sub_b_a = div_b ? div_b->a.as<Sub>() : nullptr;

        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();

        if (is_zero(b)) {
            expr = a;
        } else if (equal(a, b)) {
            expr = make_zero(op->type);
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            if (no_overflow(a.type()) &&
                sub_would_overflow(a.type().bits(), ia, ib)) {
                expr = signed_integer_overflow_error(a.type());
            } else {
                expr = IntImm::make(a.type(), ia - ib);
            }
        } else if (const_uint(a, &ua) && const_uint(b, &ub)) {
            expr = UIntImm::make(a.type(), ua - ub);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = FloatImm::make(a.type(), fa - fb);
        } else if (const_int(b, &ib)) {
            expr = mutate(a + IntImm::make(a.type(), (-ib)));
        } else if (const_float(b, &fb)) {
            expr = mutate(a + FloatImm::make(a.type(), (-fb)));
        } else if (ramp_a && ramp_b) {
            // Ramp - Ramp
            expr = mutate(Ramp::make(ramp_a->base - ramp_b->base,
                                   ramp_a->stride - ramp_b->stride, ramp_a->lanes));
        } else if (ramp_a && broadcast_b) {
            // Ramp - Broadcast
            expr = mutate(Ramp::make(ramp_a->base - broadcast_b->value,
                                   ramp_a->stride, ramp_a->lanes));
        } else if (broadcast_a && ramp_b) {
            // Broadcast - Ramp
            expr = mutate(Ramp::make(broadcast_a->value - ramp_b->base,
                                   make_zero(ramp_b->stride.type())- ramp_b->stride,
                                   ramp_b->lanes));
        } else if (broadcast_a && broadcast_b) {
            // Broadcast + Broadcast
            expr = Broadcast::make(mutate(broadcast_a->value - broadcast_b->value),
                                   broadcast_a->lanes);
        } else if (select_a && select_b &&
                   equal(select_a->condition, select_b->condition)) {
            // select(c, a, b) - select(c, d, e) -> select(c, a+d, b+e)
            expr = mutate(Select::make(select_a->condition,
                                       select_a->true_value - select_b->true_value,
                                       select_a->false_value - select_b->false_value));
        } else if (select_a &&
                   equal(select_a->true_value, b)) {
            // select(c, a, b) - a -> select(c, 0, b-a)
            expr = mutate(Select::make(select_a->condition,
                                       make_zero(op->type),
                                       select_a->false_value - select_a->true_value));
        } else if (select_a &&
                   equal(select_a->false_value, b)) {
            // select(c, a, b) - b -> select(c, a-b, 0)
            expr = mutate(Select::make(select_a->condition,
                                       select_a->true_value - select_a->false_value,
                                       make_zero(op->type)));
        } else if (select_b &&
                   equal(select_b->true_value, a)) {
            // a - select(c, a, b) -> select(c, 0, a-b)
            expr = mutate(Select::make(select_b->condition,
                                       make_zero(op->type),
                                       select_b->true_value - select_b->false_value));
        } else if (select_b &&
                   equal(select_b->false_value, a)) {
            // b - select(c, a, b) -> select(c, b-a, 0)
            expr = mutate(Select::make(select_b->condition,
                                       select_b->false_value - select_b->true_value,
                                       make_zero(op->type)));
        } else if (add_a && equal(add_a->b, b)) {
            // Ternary expressions where a term cancels
            expr = add_a->a;
        } else if (add_a &&
                   equal(add_a->a, b)) {
            expr = add_a->b;
        } else if (add_b &&
                   equal(add_b->b, a)) {
            expr = mutate(make_zero(add_b->a.type()) - add_b->a);
        } else if (add_b &&
                   equal(add_b->a, a)) {
            expr = mutate(make_zero(add_b->a.type()) - add_b->b);

        } else if (max_a &&
                   equal(max_a->a, b) &&
                   !is_const(b) &&
                   no_overflow(op->type)) {
            // max(a, b) - a -> max(0, b-a)
            expr = mutate(Max::make(make_zero(op->type), max_a->b - max_a->a));
        } else if (min_a &&
                   equal(min_a->a, b) &&
                   !is_const(b) &&
                   no_overflow(op->type)) {
            // min(a, b) - a -> min(0, b-a)
            expr = mutate(Min::make(make_zero(op->type), min_a->b - min_a->a));
        } else if (max_a &&
                   equal(max_a->b, b) &&
                   !is_const(b) &&
                   no_overflow(op->type)) {
            // max(a, b) - b -> max(a-b, 0)
            expr = mutate(Max::make(max_a->a - max_a->b, make_zero(op->type)));
        } else if (min_a &&
                   equal(min_a->b, b) &&
                   !is_const(b) &&
                   no_overflow(op->type)) {
            // min(a, b) - b -> min(a-b, 0)
            expr = mutate(Min::make(min_a->a - min_a->b, make_zero(op->type)));

        } else if (max_b &&
                   equal(max_b->a, a) &&
                   !is_const(a) &&
                   no_overflow(op->type)) {
            // a - max(a, b) -> 0 - max(0, b-a) -> min(0, a-b)
            expr = mutate(Min::make(make_zero(op->type), max_b->a - max_b->b));
        } else if (min_b &&
                   equal(min_b->a, a) &&
                   !is_const(a) &&
                   no_overflow(op->type)) {
            // a - min(a, b) -> 0 - min(0, b-a) -> max(0, a-b)
            expr = mutate(Max::make(make_zero(op->type), min_b->a - min_b->b));
        } else if (max_b &&
                   equal(max_b->b, a) &&
                   !is_const(a) &&
                   no_overflow(op->type)) {
            // b - max(a, b) -> 0 - max(a-b, 0) -> min(b-a, 0)
            expr = mutate(Min::make(max_b->b - max_b->a, make_zero(op->type)));
        } else if (min_b &&
                   equal(min_b->b, a) &&
                   !is_const(a) &&
                   no_overflow(op->type)) {
            // b - min(a, b) -> 0 - min(a-b, 0) -> max(b-a, 0)
            expr = mutate(Max::make(min_b->b - min_b->a, make_zero(op->type)));

        } else if (add_a &&
                   is_simple_const(add_a->b)) {
            // In ternary expressions, pull constants outside
            if (is_simple_const(b)) {
                expr = mutate(add_a->a + (add_a->b - b));
            } else {
                expr = mutate((add_a->a - b) + add_a->b);
            }
        } else if (sub_a &&
                   sub_b &&
                   is_const(sub_a->a) &&
                   is_const(sub_b->a)) {
            // (c1 - a) - (c2 - b) -> (b - a) + (c1 - c2)
            expr = mutate((sub_b->b - sub_a->b) + (sub_a->a - sub_b->a));
        } else if (sub_b) {
            // a - (b - c) -> a + (c - b)
            expr = mutate(a + (sub_b->b - sub_b->a));
        } else if (mul_b &&
                   is_negative_negatable_const(mul_b->b)) {
            // a - b*-x -> a + b*x
            expr = mutate(a + mul_b->a * (-mul_b->b));
        } else if (mul_b &&
                   !is_const(a) &&
                   equal(a, mul_b->a) &&
                   no_overflow(op->type)) {
            // a - a*b -> a*(1 - b)
            expr = mutate(a * (make_one(op->type) - mul_b->b));
        } else if (mul_b &&
                   !is_const(a) &&
                   equal(a, mul_b->b) &&
                   no_overflow(op->type)) {
            // a - b*a -> (1 - b)*a
            expr = mutate((make_one(op->type) - mul_b->a) * a);
        } else if (mul_a &&
                   !is_const(b) &&
                   equal(mul_a->a, b) &&
                   no_overflow(op->type)) {
            // a*b - a -> a*(b - 1)
            expr = mutate(mul_a->a * (mul_a->b - make_one(op->type)));
        } else if (mul_a &&
                   !is_const(b) &&
                   equal(mul_a->b, b) &&
                   no_overflow(op->type)) {
            // a*b - b -> (a - 1)*b
            expr = mutate((mul_a->a - make_one(op->type)) * b);
        } else if (add_b &&
                   is_simple_const(add_b->b)) {
            expr = mutate((a - add_b->a) - add_b->b);
        } else if (sub_a &&
                   is_simple_const(sub_a->a) &&
                   is_simple_const(b)) {
            expr = mutate((sub_a->a - b) - sub_a->b);
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->a, mul_b->a)) {
            // Pull out common factors a*x + b*x
            expr = mutate(mul_a->a * (mul_a->b - mul_b->b));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->b, mul_b->a)) {
            expr = mutate(mul_a->b * (mul_a->a - mul_b->b));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->b, mul_b->b)) {
            expr = mutate(mul_a->b * (mul_a->a - mul_b->a));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->a, mul_b->b)) {
            expr = mutate(mul_a->a * (mul_a->b - mul_b->a));
        } else if (add_a &&
                   add_b &&
                   equal(add_a->b, add_b->b)) {
            // Quaternary expressions where a term cancels
            // (a + b) - (c + b) -> a - c
            expr = mutate(add_a->a - add_b->a);
        } else if (add_a &&
                   add_b &&
                   equal(add_a->a, add_b->a)) {
            // (a + b) - (a + c) -> b - c
            expr = mutate(add_a->b - add_b->b);
        } else if (add_a &&
                   add_b &&
                   equal(add_a->a, add_b->b)) {
            // (a + b) - (c + a) -> b - c
            expr = mutate(add_a->b - add_b->a);
        } else if (add_a &&
                   add_b &&
                   equal(add_a->b, add_b->a)) {
            // (b + a) - (a + c) -> b - c
            expr = mutate(add_a->a - add_b->b);
        } else if (no_overflow(op->type) &&
                   min_b &&
                   add_b_a &&
                   no_overflow(op->type) &&
                   equal(a, add_b_a->a)) {
            // Quaternary expressions involving mins where a term
            // cancels. These are important for bounds inference
            // simplifications.
            // a - min(a + b, c) -> max(-b, a-c)
            expr = mutate(max(0 - add_b_a->b, a - min_b->b));
        } else if (no_overflow(op->type) &&
                   min_b &&
                   add_b_a &&
                   no_overflow(op->type) &&
                   equal(a, add_b_a->b)) {
            // a - min(b + a, c) -> max(-b, a-c)
            expr = mutate(max(0 - add_b_a->a, a - min_b->b));
        } else if (no_overflow(op->type) &&
                   min_b &&
                   add_b_b &&
                   equal(a, add_b_b->a)) {
            // a - min(c, a + b) -> max(-b, a-c)
            expr = mutate(max(0 - add_b_b->b, a - min_b->a));
        } else if (no_overflow(op->type) &&
                   min_b &&
                   add_b_b &&
                   equal(a, add_b_b->b)) {
            // a - min(c, b + a) -> max(-b, a-c)
            expr = mutate(max(0 - add_b_b->a, a - min_b->a));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_a &&
                   equal(b, add_a_a->a)) {
            // min(a + b, c) - a -> min(b, c-a)
            expr = mutate(min(add_a_a->b, min_a->b - b));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_a &&
                   equal(b, add_a_a->b)) {
            // min(b + a, c) - a -> min(b, c-a)
            expr = mutate(min(add_a_a->a, min_a->b - b));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_b &&
                   equal(b, add_a_b->a)) {
            // min(c, a + b) - a -> min(b, c-a)
            expr = mutate(min(add_a_b->b, min_a->a - b));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_b &&
                   equal(b, add_a_b->b)) {
            // min(c, b + a) - a -> min(b, c-a)
            expr = mutate(min(add_a_b->a, min_a->a - b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->b) &&
                   equal(min_a->b, min_b->a)) {
            // min(a, b) - min(b, a) -> 0
            expr = make_zero(op->type);
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->b) &&
                   equal(max_a->b, max_b->a)) {
            // max(a, b) - max(b, a) -> 0
            expr = make_zero(op->type);
        } else if (min_a &&
                   min_b &&
                   is_zero(mutate((min_a->a + min_b->b) - (min_a->b + min_b->a)))) {
            // min(a, b) - min(c, d) where a-b == c-d -> b - d
            expr = mutate(min_a->b - min_b->b);
        } else if (max_a &&
                   max_b &&
                   is_zero(mutate((max_a->a + max_b->b) - (max_a->b + max_b->a)))) {
            // max(a, b) - max(c, d) where a-b == c-d -> b - d
            expr = mutate(max_a->b - max_b->b);
        } else if (min_a &&
                   min_b &&
                   is_zero(mutate((min_a->a + min_b->a) - (min_a->b + min_b->b)))) {
            // min(a, b) - min(c, d) where a-b == d-c -> b - c
            expr = mutate(min_a->b - min_b->a);
        } else if (max_a &&
                   max_b &&
                   is_zero(mutate((max_a->a + max_b->a) - (max_a->b + max_b->b)))) {
            // max(a, b) - max(c, d) where a-b == d-c -> b - c
            expr = mutate(max_a->b - max_b->a);
        } else if (no_overflow(op->type) &&
                   (op->type.is_int() || op->type.is_uint()) &&
                   mul_a &&
                   div_a_a &&
                   is_positive_const(mul_a->b) &&
                   equal(mul_a->b, div_a_a->b) &&
                   equal(div_a_a->a, b)) {
            // (x/4)*4 - x -> -(x%4)
            expr = mutate(make_zero(a.type()) - (b % mul_a->b));
        } else if (no_overflow(op->type) &&
                   (op->type.is_int() || op->type.is_uint()) &&
                   mul_b &&
                   div_b_a &&
                   is_positive_const(mul_b->b) &&
                   equal(mul_b->b, div_b_a->b) &&
                   equal(div_b_a->a, a)) {
            // x - (x/4)*4 -> x%4
            expr = mutate(a % mul_b->b);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   add_a_a &&
                   add_b_a &&
                   equal(add_a_a->a, add_b_a->a) &&
                   (is_simple_const(add_a_a->b) ||
                    is_simple_const(add_b_a->b))) {
            // This pattern comes up in bounds inference on upsampling code:
            // (x + a)/c - (x + b)/c ->
            //    ((c + a - 1 - b) - (x + a)%c)/c (duplicates a)
            // or ((x + b)%c + (a - b))/c         (duplicates b)
            Expr x = add_a_a->a, a = add_a_a->b, b = add_b_a->b, c = div_a->b;
            if (is_simple_const(b)) {
                // Use the version that injects two copies of b
                expr = mutate((((x + (b % c)) % c) + (a - b))/c);
            } else {
                // Use the version that injects two copies of a
                expr = mutate((((c + a - 1) - b) - ((x + (a % c)) % c))/c);
            }
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   add_b_a &&
                   equal(div_a->a, add_b_a->a)) {
            // Same as above, where a == 0
            Expr x = div_a->a, b = add_b_a->b, c = div_a->b;
            expr = mutate(((c - 1 - b) - (x % c))/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   add_a_a &&
                   equal(add_a_a->a, div_b->a)) {
            // Same as above, where b == 0
            Expr x = add_a_a->a, a = add_a_a->b, c = div_a->b;
            expr = mutate(((x % c) + a)/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   sub_b_a &&
                   equal(div_a->a, sub_b_a->a)) {
            // Same as above, where a == 0 and b is subtracted
            Expr x = div_a->a, b = sub_b_a->b, c = div_a->b;
            expr = mutate(((c - 1 + b) - (x % c))/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   sub_a_a &&
                   equal(sub_a_a->a, div_b->a)) {
            // Same as above, where b == 0, and a is subtracted
            Expr x = sub_a_a->a, a = sub_a_a->b, c = div_a->b;
            expr = mutate(((x % c) - a)/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   sub_a_a &&
                   add_b_a &&
                   equal(sub_a_a->a, add_b_a->a) &&
                   is_simple_const(add_b_a->b)) {
            // Same as above, where a is subtracted and b is a constant
            // (x - a)/c - (x + b)/c -> ((x + b)%c - a - b)/c
            Expr x = sub_a_a->a, a = sub_a_a->b, b = add_b_a->b, c = div_a->b;
            expr = mutate((((x + (b % c)) % c) - a - b)/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   add_a_a &&
                   sub_b_a &&
                   equal(add_a_a->a, sub_b_a->a) &&
                   is_simple_const(add_a_a->b)) {
            // Same as above, where b is subtracted and a is a constant
            // (x + a)/c - (x - b)/c -> (b - (x + a)%c + (a + c - 1))/c
            Expr x = add_a_a->a, a = add_a_a->b, b = sub_b_a->b, c = div_a->b;
            expr = mutate((b - (x + (a % c))%c + (a + c - 1))/c);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Sub::make(a, b);
        }
    }

    void visit(const Mul *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        if (is_simple_const(a)) std::swap(a, b);

        int64_t ia = 0, ib = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();

        if (is_zero(a)) {
            expr = a;
        } else if (is_zero(b)) {
            expr = b;
        } else if (is_one(a)) {
            expr = b;
        } else if (is_one(b)) {
            expr = a;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            if (no_overflow(a.type()) &&
                mul_would_overflow(a.type().bits(), ia, ib)) {
                expr = signed_integer_overflow_error(a.type());
            } else {
                expr = IntImm::make(a.type(), ia * ib);
            }
        } else if (const_uint(a, &ua) && const_uint(b, &ub)) {
            expr = UIntImm::make(a.type(), ua * ub);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = FloatImm::make(a.type(), fa * fb);
        } else if (broadcast_a && broadcast_b) {
            expr = Broadcast::make(mutate(broadcast_a->value * broadcast_b->value), broadcast_a->lanes);
        } else if (ramp_a && broadcast_b) {
            Expr m = broadcast_b->value;
            expr = mutate(Ramp::make(ramp_a->base * m, ramp_a->stride * m, ramp_a->lanes));
        } else if (broadcast_a && ramp_b) {
            Expr m = broadcast_a->value;
            expr = mutate(Ramp::make(m * ramp_b->base, m * ramp_b->stride, ramp_b->lanes));
        } else if (add_a &&
                   !(add_a->b.as<Ramp>() && ramp_b) &&
                   is_simple_const(add_a->b) &&
                   is_simple_const(b)) {
            expr = mutate(add_a->a * b + add_a->b * b);
        } else if (sub_a && is_negative_negatable_const(b)) {
            expr = mutate(Mul::make(Sub::make(sub_a->b, sub_a->a), -b));
        } else if (mul_a && is_simple_const(mul_a->b) && is_simple_const(b)) {
            expr = mutate(mul_a->a * (mul_a->b * b));
        } else if (mul_b && is_simple_const(mul_b->b)) {
            // Pull constants outside
            expr = mutate((a * mul_b->a) * mul_b->b);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Mul::make(a, b);
        }
    }

    void visit(const Div *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        int64_t ia = 0, ib = 0, ic = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;

        const Mul *mul_a = a.as<Mul>();
        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Div *div_a = a.as<Div>();
        const Div *div_a_a = nullptr;
        const Mul *mul_a_a = nullptr;
        const Mul *mul_a_b = nullptr;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Ramp *ramp_a = a.as<Ramp>();
        const Broadcast *broadcast_b = b.as<Broadcast>();

        if (add_a) {
            div_a_a = add_a->a.as<Div>();
            mul_a_a = add_a->a.as<Mul>();
            mul_a_b = add_a->b.as<Mul>();
        } else if (sub_a) {
            mul_a_a = sub_a->a.as<Mul>();
            mul_a_b = sub_a->b.as<Mul>();
        }

        if (ramp_a) {
            mul_a_a = ramp_a->base.as<Mul>();
        }

        // Check for bounded numerators divided by constant
        // denominators.
        int64_t num_min, num_max;
        if (const_int(b, &ib) && ib &&
            const_int_bounds(a, &num_min, &num_max) &&
            div_imp(num_max, ib) == div_imp(num_min, ib)) {
            expr = make_const(op->type, div_imp(num_max, ib));
            return;
        }

        ModulusRemainder mod_rem(0, 1);
        if (ramp_a && no_overflow_scalar_int(ramp_a->base.type())) {
            // Do modulus remainder analysis on the base.
            mod_rem = modulus_remainder(ramp_a->base, alignment_info);
        }

        if (is_zero(a) && !is_zero(b)) {
            expr = a;
        } else if (is_one(b)) {
            expr = a;
        } else if (equal(a, b) &&
                   !is_zero(b)) {
            expr = make_one(op->type);
        } else if (const_int(a, &ia) &&
                   const_int(b, &ib) && ib) {
            expr = IntImm::make(op->type, div_imp(ia, ib));
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub) && ub) {
            expr = UIntImm::make(op->type, ua / ub);
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb) &&
                   fb != 0.0f) {
            expr = FloatImm::make(op->type, fa / fb);
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(Broadcast::make(Div::make(broadcast_a->value, broadcast_b->value), broadcast_a->lanes));
        } else if (ramp_a &&
                   no_overflow_scalar_int(ramp_a->base.type()) &&
                   const_int(ramp_a->stride, &ia) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib &&
                   ia % ib == 0) {
            // ramp(x, 4, w) / broadcast(2, w) -> ramp(x / 2, 2, w)
            Type t = op->type.element_of();
            expr = mutate(Ramp::make(ramp_a->base / broadcast_b->value,
                                     IntImm::make(t, div_imp(ia, ib)),
                                     ramp_a->lanes));
        } else if (ramp_a &&
                   no_overflow_scalar_int(ramp_a->base.type()) &&
                   const_int(ramp_a->stride, &ia) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib != 0 &&
                   mod_rem.modulus % ib == 0 &&
                   div_imp((int64_t)mod_rem.remainder, ib) == div_imp(mod_rem.remainder + (ramp_a->lanes-1)*ia, ib)) {
            // ramp(k*z + x, y, w) / z = broadcast(k, w) if x/z == (x + (w-1)*y)/z
            expr = mutate(Broadcast::make(ramp_a->base / broadcast_b->value, ramp_a->lanes));
        } else if (no_overflow(op->type) &&
                   div_a &&
                   const_int(div_a->b, &ia) &&
                   ia >= 0 &&
                   const_int(b, &ib) &&
                   ib >= 0) {
            // (x / 3) / 4 -> x / 12
            expr = mutate(div_a->a / make_const(op->type, ia * ib));
        } else if (no_overflow(op->type) &&
                   div_a_a &&
                   add_a &&
                   const_int(div_a_a->b, &ia) &&
                   ia >= 0 &&
                   const_int(add_a->b, &ib) &&
                   const_int(b, &ic) &&
                   ic >= 0) {
            // (x / ia + ib) / ic -> (x + ia*ib) / (ia*ic)
            expr = mutate((div_a_a->a + make_const(op->type, ia*ib)) / make_const(op->type, ia*ic));
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   const_int(mul_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia > 0 &&
                   ib > 0 &&
                   (ia % ib == 0 || ib % ia == 0)) {
            if (ia % ib == 0) {
                // (x * 4) / 2 -> x * 2
                expr = mutate(mul_a->a * make_const(op->type, div_imp(ia, ib)));
            } else {
                // (x * 2) / 4 -> x / 2
                expr = mutate(mul_a->a / make_const(op->type, div_imp(ib, ia)));
            }
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_a &&
                   const_int(mul_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // Pull terms that are a multiple of the divisor out
            // (x*4 + y) / 2 -> x*2 + y/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            expr = mutate((mul_a_a->a * ratio) + (add_a->b / b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_b &&
                   const_int(mul_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (y + x*4) / 2 -> y/2 + x*2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            expr = mutate((add_a->a / b) + (mul_a_b->a * ratio));
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   mul_a_a &&
                   const_int(mul_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // Pull terms that are a multiple of the divisor out
            // (x*4 - y) / 2 -> x*2 - y/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            expr = mutate((mul_a_a->a * ratio) - (sub_a->b / b));
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   mul_a_b &&
                   const_int(mul_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (y - x*4) / 2 -> y/2 - x*2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            expr = mutate((sub_a->a / b) - (mul_a_b->a * ratio));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   const_int(add_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (y + 8) / 2 -> y/2 + 4
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            expr = mutate((add_a->a / b) + ratio);
        } else if (b.type().is_float() && is_simple_const(b)) {
            // Convert const float division to multiplication
            // x / 2 -> x * 0.5
            expr = mutate(a * (make_one(b.type()) / b));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Div::make(a, b);
        }
    }

    void visit(const Mod *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        int64_t ia = 0, ib = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Mul *mul_a = a.as<Mul>();
        const Add *add_a = a.as<Add>();
        const Mul *mul_a_a = add_a ? add_a->a.as<Mul>() : nullptr;
        const Mul *mul_a_b = add_a ? add_a->b.as<Mul>() : nullptr;
        const Ramp *ramp_a = a.as<Ramp>();

        // If the RHS is a constant, do modulus remainder analysis on the LHS
        ModulusRemainder mod_rem(0, 1);

        if (const_int(b, &ib) &&
            ib &&
            no_overflow_scalar_int(op->type)) {

            // If the LHS is bounded, we can possibly bail out early
            int64_t a_min, a_max;
            if (const_int_bounds(a, &a_min, &a_max) &&
                a_max < ib && a_min >= 0) {
                expr = a;
                return;
            }

            mod_rem = modulus_remainder(a, alignment_info);
        }

        // If the RHS is a constant and the LHS is a ramp, do modulus
        // remainder analysis on the base.
        if (broadcast_b &&
            const_int(broadcast_b->value, &ib) &&
            ib &&
            ramp_a &&
            no_overflow_scalar_int(ramp_a->base.type())) {
            mod_rem = modulus_remainder(ramp_a->base, alignment_info);
        }

        if (is_zero(a) && !is_zero(b)) {
            expr = a;
        } else if (const_int(a, &ia) && const_int(b, &ib) && ib) {
            expr = IntImm::make(op->type, mod_imp(ia, ib));
        } else if (const_uint(a, &ua) && const_uint(b, &ub) && ub) {
            expr = UIntImm::make(op->type, ua % ub);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = FloatImm::make(op->type, mod_imp(fa, fb));
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(Broadcast::make(Mod::make(broadcast_a->value, broadcast_b->value), broadcast_a->lanes));
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   const_int(b, &ib) &&
                   ib &&
                   const_int(mul_a->b, &ia) &&
                   (ia % ib == 0)) {
            // (x * (b*a)) % b -> 0
            expr = make_zero(op->type);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_a &&
                   const_int(mul_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib &&
                   (ia % ib == 0)) {
            // (x * (b*a) + y) % b -> (y % b)
            expr = mutate(add_a->b % b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   const_int(add_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib &&
                   (ia % ib == 0)) {
            // (y + (b*a)) % b -> (y % b)
            expr = mutate(add_a->a % b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_b &&
                   const_int(mul_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ib &&
                   (ia % ib == 0)) {
            // (y + x * (b*a)) % b -> (y % b)
            expr = mutate(add_a->a % b);
        } else if (no_overflow_scalar_int(op->type) &&
                   const_int(b, &ib) &&
                   ib &&
                   mod_rem.modulus % ib == 0) {
            // ((a*b)*x + c) % a -> c % a
            expr = make_const(op->type, mod_imp((int64_t)mod_rem.remainder, ib));
        } else if (no_overflow(op->type) &&
                   ramp_a &&
                   const_int(ramp_a->stride, &ia) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib &&
                   ia % ib == 0) {
            // ramp(x, 4, w) % broadcast(2, w)
            expr = mutate(Broadcast::make(ramp_a->base % broadcast_b->value, ramp_a->lanes));
        } else if (ramp_a &&
                   no_overflow_scalar_int(ramp_a->base.type()) &&
                   const_int(ramp_a->stride, &ia) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib != 0 &&
                   mod_rem.modulus % ib == 0 &&
                   div_imp((int64_t)mod_rem.remainder, ib) == div_imp(mod_rem.remainder + (ramp_a->lanes-1)*ia, ib)) {
            // ramp(k*z + x, y, w) % z = ramp(x, y, w) if x/z == (x + (w-1)*y)/z
            Expr new_base = make_const(ramp_a->base.type(), mod_imp((int64_t)mod_rem.remainder, ib));
            expr = mutate(Ramp::make(new_base, ramp_a->stride, ramp_a->lanes));
        } else if (ramp_a &&
                   no_overflow_scalar_int(ramp_a->base.type()) &&
                   const_int(ramp_a->stride, &ia) &&
                   !is_const(ramp_a->base) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib != 0 &&
                   mod_rem.modulus % ib == 0) {
            // ramp(k*z + x, y, w) % z = ramp(x, y, w) % z
            Type t = ramp_a->base.type();
            Expr new_base = make_const(t, mod_imp((int64_t)mod_rem.remainder, ib));
            expr = mutate(Ramp::make(new_base, ramp_a->stride, ramp_a->lanes) % b);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Mod::make(a, b);
        }
    }

    void visit(const Min *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        // Move constants to the right to cut down on number of cases to check
        if (is_simple_const(a) && !is_simple_const(b)) {
            std::swap(a, b);
        }

        int64_t ia = 0, ib = 0, ic = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;
        int64_t a_min, a_max, b_min, b_max;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Ramp *ramp_a = a.as<Ramp>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Min *min_a = a.as<Min>();
        const Min *min_b = b.as<Min>();
        const Min *min_a_a = min_a ? min_a->a.as<Min>() : nullptr;
        const Min *min_a_a_a = min_a_a ? min_a_a->a.as<Min>() : nullptr;
        const Min *min_a_a_a_a = min_a_a_a ? min_a_a_a->a.as<Min>() : nullptr;
        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();
        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();
        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();

        min_a_a = max_a ? max_a->a.as<Min>() : min_a_a;

        // Detect if the lhs or rhs is a rounding-up operation
        int64_t a_round_up_factor = 0, b_round_up_factor = 0;
        Expr a_round_up = is_round_up(a, &a_round_up_factor);
        Expr b_round_up = is_round_up(b, &b_round_up_factor);

        if (equal(a, b)) {
            expr = a;
            return;
        } else if (const_int(a, &ia) &&
                   const_int(b, &ib)) {
            expr = IntImm::make(op->type, std::min(ia, ib));
            return;
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            expr = UIntImm::make(op->type, std::min(ua, ub));
            return;
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb)) {
            expr = FloatImm::make(op->type, std::min(fa, fb));
            return;
        } else if (const_int(b, &ib) &&
                   b.type().is_max(ib)) {
            // Compute minimum of expression of type and maximum of type --> expression
            expr = a;
            return;
        } else if (const_int(b, &ib) &&
                   b.type().is_min(ib)) {
            // Compute minimum of expression of type and minimum of type --> min of type
            expr = b;
            return;
        } else if (const_uint(b, &ub) &&
                   b.type().is_max(ub)) {
            // Compute minimum of expression of type and maximum of type --> expression
            expr = a;
            return;
        } else if (op->type.is_uint() &&
                   is_zero(b)) {
            // Compute minimum of expression of type and minimum of type --> min of type
            expr = b;
            return;
        } else if (broadcast_a &&
                   broadcast_b) {
            expr = mutate(Broadcast::make(Min::make(broadcast_a->value, broadcast_b->value), broadcast_a->lanes));
            return;
        } else if (const_int_bounds(a, &a_min, &a_max) &&
                   const_int_bounds(b, &b_min, &b_max)) {
            if (a_min >= b_max) {
                expr = b;
                return;
            } else if (b_min >= a_max) {
                expr = a;
                return;
            }
        } else if (no_overflow(op->type) &&
                   ramp_a &&
                   broadcast_b &&
                   const_int(ramp_a->base, &ia) &&
                   const_int(ramp_a->stride, &ib) &&
                   const_int(broadcast_b->value, &ic)) {
            // min(ramp(a, b, n), broadcast(c, n))
            int ramp_start = ia;
            int ramp_end = ia + ib * (ramp_a->lanes - 1);
            if (ramp_start <= ic && ramp_end <= ic) {
                // ramp dominates
                expr = a;
                return;
            } if (ramp_start >= ic && ramp_end >= ic) {
                // broadcast dominates
                expr = b;
                return;
            }
        }

        if (no_overflow(op->type) &&
            add_a &&
            const_int(add_a->b, &ia) &&
            add_b &&
            const_int(add_b->b, &ib) &&
            equal(add_a->a, add_b->a)) {
            // min(x + 3, x - 2) -> x - 2
            if (ia > ib) {
                expr = b;
            } else {
                expr = a;
            }
        } else if (no_overflow(op->type) &&
                   add_a &&
                   const_int(add_a->b, &ia) &&
                   equal(add_a->a, b)) {
            // min(x + 5, x) -> x
            if (ia > 0) {
                expr = b;
            } else {
                expr = a;
            }
        } else if (no_overflow(op->type) &&
                   add_b &&
                   const_int(add_b->b, &ib) &&
                   equal(add_b->a, a)) {
            // min(x, x + 5) -> x
            if (ib > 0) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   sub_b &&
                   equal(sub_a->b, sub_b->b) &&
                   const_int(sub_a->a, &ia) &&
                   const_int(sub_b->a, &ib)) {
            // min (100-x, 101-x) -> 100-x
            if (ia < ib) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (a_round_up.defined() &&
                   equal(a_round_up, b)) {
            // min(((a + 3)/4)*4, a) -> a
            expr = b;
        } else if (a_round_up.defined() &&
                   max_b &&
                   equal(a_round_up, max_b->a) &&
                   is_const(max_b->b, a_round_up_factor)) {
            // min(((a + 3)/4)*4, max(a, 4)) -> max(a, 4)
            expr = b;
        } else if (b_round_up.defined() &&
                   equal(b_round_up, a)) {
            // min(a, ((a + 3)/4)*4) -> a
            expr = a;
        } else if (b_round_up.defined() &&
                   max_a &&
                   equal(b_round_up, max_a->a) &&
                   is_const(max_a->b, b_round_up_factor)) {
            // min(max(a, 4), ((a + 3)/4)*4) -> max(a, 4)
            expr = a;
        } else if (max_a &&
                   equal(max_a->b, b)) {
            // min(max(x, y), y) -> y
            expr = b;
        } else if (min_a &&
                   (equal(min_a->b, b) || equal(min_a->a, b))) {
            // min(min(x, y), y) -> min(x, y)
            expr = a;
        } else if (min_b &&
                   (equal(min_b->b, a) || equal(min_b->a, a))) {
            // min(y, min(x, y)) -> min(x, y)
            expr = b;
        } else if (min_a &&
                   min_a_a &&
                   equal(min_a_a->b, b)) {
            // min(min(min(x, y), z), y) -> min(min(x, y), z)
            expr = a;
        } else if (min_a &&
                   min_a_a_a &&
                   equal(min_a_a_a->b, b)) {
            // min(min(min(min(x, y), z), w), y) -> min(min(min(x, y), z), w)
            expr = a;
        } else if (min_a &&
                   min_a_a_a_a &&
                   equal(min_a_a_a_a->b, b)) {
            // min(min(min(min(min(x, y), z), w), l), y) -> min(min(min(min(x, y), z), w), l)
            expr = a;
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->a)) {
            // Distributive law for min/max
            // min(max(x, y), max(x, z)) -> max(min(y, z), x)
            expr = mutate(Max::make(Min::make(max_a->b, max_b->b), max_a->a));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->b)) {
            // min(max(x, y), max(z, x)) -> max(min(y, z), x)
            expr = mutate(Max::make(Min::make(max_a->b, max_b->a), max_a->a));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->b, max_b->a)) {
            // min(max(y, x), max(x, z)) -> max(min(y, z), x)
            expr = mutate(Max::make(Min::make(max_a->a, max_b->b), max_a->b));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->b, max_b->b)) {
            // min(max(y, x), max(z, x)) -> max(min(y, z), x)
            expr = mutate(Max::make(Min::make(max_a->a, max_b->a), max_a->b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->a)) {
            // min(min(x, y), min(x, z)) -> min(min(y, z), x)
            expr = mutate(Min::make(Min::make(min_a->b, min_b->b), min_a->a));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->b)) {
            // min(min(x, y), min(z, x)) -> min(min(y, z), x)
            expr = mutate(Min::make(Min::make(min_a->b, min_b->a), min_a->a));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->b, min_b->a)) {
            // min(min(y, x), min(x, z)) -> min(min(y, z), x)
            expr = mutate(Min::make(Min::make(min_a->a, min_b->b), min_a->b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->b, min_b->b)) {
            // min(min(y, x), min(z, x)) -> min(min(y, z), x)
            expr = mutate(Min::make(Min::make(min_a->a, min_b->a), min_a->b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->b, add_b->b)) {
            // Distributive law for addition
            // min(a + b, c + b) -> min(a, c) + b
            expr = mutate(min(add_a->a, add_b->a)) + add_a->b;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->a, add_b->a)) {
            // min(b + a, b + c) -> min(a, c) + b
            expr = mutate(min(add_a->b, add_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->a, add_b->b)) {
            // min(b + a, c + b) -> min(a, c) + b
            expr = mutate(min(add_a->b, add_b->a)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->b, add_b->a)) {
            // min(a + b, b + c) -> min(a, c) + b
            expr = mutate(min(add_a->a, add_b->b)) + add_a->b;
        } else if (min_a &&
                   is_simple_const(min_a->b)) {
            if (is_simple_const(b)) {
                // min(min(x, 4), 5) -> min(x, 4)
                expr = Min::make(min_a->a, mutate(Min::make(b, min_a->b)));
            } else {
                // min(min(x, 4), y) -> min(min(x, y), 4)
                expr = mutate(Min::make(Min::make(min_a->a, b), min_a->b));
            }
        } else if (no_overflow(op->type) &&
                   div_a &&
                   div_b &&
                   const_int(div_a->b, &ia) &&
                   ia &&
                   const_int(div_b->b, &ib) &&
                   (ia == ib)) {
            // min(a / 4, b / 4) -> min(a, b) / 4
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                expr = mutate(min(div_a->a, div_b->a) / factor);
            } else {
                expr = mutate(max(div_a->a, div_b->a) / factor);
            }
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   (ia == ib)) {
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                expr = mutate(min(mul_a->a, mul_b->a) * factor);
            } else {
                expr = mutate(max(mul_a->a, mul_b->a) * factor);
            }
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   const_int(mul_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia &&
                   (ib % ia == 0)) {
            // min(x*8, 24) -> min(x, 3)*8
            Expr ratio  = make_const(op->type, ib / ia);
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                expr = mutate(min(mul_a->a, ratio) * factor);
            } else {
                expr = mutate(max(mul_a->a, ratio) * factor);
            }
        } else if (call_a &&
                   call_a->is_intrinsic(Call::likely) &&
                   equal(call_a->args[0], b)) {
            // min(likely(b), b) -> likely(b)
            expr = a;
        } else if (call_b &&
                   call_b->is_intrinsic(Call::likely) &&
                   equal(call_b->args[0], a)) {
            // min(a, likely(a)) -> likely(a)
            expr = b;
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   is_const(sub_a->a) &&
                   is_const(b)) {
            // min(8 - x, 3) -> 8 - max(x, 5)
            expr = mutate(sub_a->a - max(sub_a->b, sub_a->a - b));
        } else if (select_a &&
                   select_b &&
                   equal(select_a->condition, select_b->condition)) {
            expr = mutate(select(select_a->condition,
                                 min(select_a->true_value, select_b->true_value),
                                 min(select_a->false_value, select_b->false_value)));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Min::make(a, b);
        }
    }

    void visit(const Max *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        // Move constants to the right to cut down on number of cases to check
        if (is_simple_const(a) && !is_simple_const(b)) {
            std::swap(a, b);
        }


        int64_t ia = 0, ib = 0, ic = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;
        int64_t a_min, a_max, b_min, b_max;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Ramp *ramp_a = a.as<Ramp>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();
        const Max *max_a_a = max_a ? max_a->a.as<Max>() : nullptr;
        const Max *max_a_a_a = max_a_a ? max_a_a->a.as<Max>() : nullptr;
        const Max *max_a_a_a_a = max_a_a_a ? max_a_a_a->a.as<Max>() : nullptr;
        const Min *min_a = a.as<Min>();
        const Min *min_b = b.as<Min>();
        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();
        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();

        if (equal(a, b)) {
            expr = a;
            return;
        } else if (const_int(a, &ia) &&
                   const_int(b, &ib)) {
            expr = IntImm::make(op->type, std::max(ia, ib));
            return;
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            expr = UIntImm::make(op->type, std::max(ua, ub));
            return;
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb)) {
            expr = FloatImm::make(op->type, std::max(fa, fb));
            return;
        } else if (const_int(b, &ib) &&
                   b.type().is_min(ib)) {
            // Compute maximum of expression of type and minimum of type --> expression
            expr = a;
            return;
        } else if (const_int(b, &ib) &&
                   b.type().is_max(ib)) {
            // Compute maximum of expression of type and maximum of type --> max of type
            expr = b;
            return;
        } else if (op->type.is_uint() &&
                   is_zero(b)) {
            // Compute maximum of expression of type and minimum of type --> expression
            expr = a;
            return;
        } else if (const_uint(b, &ub) &&
                   b.type().is_max(ub)) {
            // Compute maximum of expression of type and maximum of type --> max of type
            expr = b;
            return;
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(Broadcast::make(Max::make(broadcast_a->value, broadcast_b->value), broadcast_a->lanes));
            return;
        } else if (const_int_bounds(a, &a_min, &a_max) &&
                   const_int_bounds(b, &b_min, &b_max)) {
            if (a_min >= b_max) {
                expr = a;
                return;
            } else if (b_min >= a_max) {
                expr = b;
                return;
            }
        } else if (no_overflow(op->type) &&
                   ramp_a &&
                   broadcast_b &&
                   const_int(ramp_a->base, &ia) &&
                   const_int(ramp_a->stride, &ib) &&
                   const_int(broadcast_b->value, &ic)) {
            // max(ramp(a, b, n), broadcast(c, n))
            int ramp_start = ia;
            int ramp_end = ia + ib * (ramp_a->lanes - 1);
            if (ramp_start >= ic && ramp_end >= ic) {
                // ramp dominates
                expr = a;
                return;
            }
            if (ramp_start <= ic && ramp_end <= ic) {
                // broadcast dominates
                expr = b;
                return;
            }
        }

        if (no_overflow(op->type) &&
            add_a &&
            const_int(add_a->b, &ia) &&
            add_b &&
            const_int(add_b->b, &ib) &&
            equal(add_a->a, add_b->a)) {
            // max(x + 3, x - 2) -> x - 2
            if (ia > ib) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (no_overflow(op->type) &&
                   add_a &&
                   const_int(add_a->b, &ia) &&
                   equal(add_a->a, b)) {
            // max(x + 5, x)
            if (ia > 0) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (no_overflow(op->type) &&
                   add_b &&
                   const_int(add_b->b, &ib) &&
                   equal(add_b->a, a)) {
            // max(x, x + 5)
            if (ib > 0) {
                expr = b;
            } else {
                expr = a;
            }
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   sub_b &&
                   equal(sub_a->b, sub_b->b) &&
                   const_int(sub_a->a, &ia) &&
                   const_int(sub_b->a, &ib)) {
            // max (100-x, 101-x) -> 101-x
            if (ia > ib) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (min_a &&
                   equal(min_a->b, b)) {
            // max(min(x, y), y) -> y
            expr = b;
        } else if (max_a &&
                   (equal(max_a->b, b) || equal(max_a->a, b))) {
            // max(max(x, y), y) -> max(x, y)
            expr = a;
        } else if (max_b &&
                   (equal(max_b->b, a) || equal(max_b->a, a))) {
            // max(y, max(x, y)) -> max(x, y)
            expr = b;
        } else if (max_a_a &&
                   equal(max_a_a->b, b)) {
            // max(max(max(x, y), z), y) -> max(max(x, y), z)
            expr = a;
        } else if (max_a_a_a &&
                   equal(max_a_a_a->b, b)) {
            // max(max(max(max(x, y), z), w), y) -> max(max(max(x, y), z), w)
            expr = a;
        } else if (max_a_a_a_a &&
                   equal(max_a_a_a_a->b, b)) {
            // max(max(max(max(max(x, y), z), w), l), y) -> max(max(max(max(x, y), z), w), l)
            expr = a;
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->a)) {
            // Distributive law for min/max
            // max(max(x, y), max(x, z)) -> max(max(y, z), x)
            expr = mutate(Max::make(Max::make(max_a->b, max_b->b), max_a->a));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->b)) {
            // max(max(x, y), max(z, x)) -> max(max(y, z), x)
            expr = mutate(Max::make(Max::make(max_a->b, max_b->a), max_a->a));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->b, max_b->a)) {
            // max(max(y, x), max(x, z)) -> max(max(y, z), x)
            expr = mutate(Max::make(Max::make(max_a->a, max_b->b), max_a->b));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->b, max_b->b)) {
            // max(max(y, x), max(z, x)) -> max(max(y, z), x)
            expr = mutate(Max::make(Max::make(max_a->a, max_b->a), max_a->b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->a)) {
            // max(min(x, y), min(x, z)) -> min(max(y, z), x)
            expr = mutate(Min::make(Max::make(min_a->b, min_b->b), min_a->a));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->b)) {
            // max(min(x, y), min(z, x)) -> min(max(y, z), x)
            expr = mutate(Min::make(Max::make(min_a->b, min_b->a), min_a->a));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->b, min_b->a)) {
            // max(min(y, x), min(x, z)) -> min(max(y, z), x)
            expr = mutate(Min::make(Max::make(min_a->a, min_b->b), min_a->b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->b, min_b->b)) {
            // max(min(y, x), min(z, x)) -> min(max(y, z), x)
            expr = mutate(Min::make(Max::make(min_a->a, min_b->a), min_a->b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->b, add_b->b)) {
            // Distributive law for addition
            // max(a + b, c + b) -> max(a, c) + b
            expr = mutate(max(add_a->a, add_b->a)) + add_a->b;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->a, add_b->a)) {
            // max(b + a, b + c) -> max(a, c) + b
            expr = mutate(max(add_a->b, add_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->a, add_b->b)) {
            // max(b + a, c + b) -> max(a, c) + b
            expr = mutate(max(add_a->b, add_b->a)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->b, add_b->a)) {
            // max(a + b, b + c) -> max(a, c) + b
            expr = mutate(max(add_a->a, add_b->b)) + add_a->b;
        } else if (max_a && is_simple_const(max_a->b)) {
            if (is_simple_const(b)) {
                // max(max(x, 4), 5) -> max(x, 4)
                expr = Max::make(max_a->a, mutate(Max::make(b, max_a->b)));
            } else {
                // max(max(x, 4), y) -> max(max(x, y), 4)
                expr = mutate(Max::make(Max::make(max_a->a, b), max_a->b));
            }
        } else if (no_overflow(op->type) &&
                   div_a &&
                   div_b &&
                   const_int(div_a->b, &ia) &&
                   ia &&
                   const_int(div_b->b, &ib) &&
                   (ia == ib)) {
            // max(a / 4, b / 4) -> max(a, b) / 4
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                expr = mutate(max(div_a->a, div_b->a) / factor);
            } else {
                expr = mutate(min(div_a->a, div_b->a) / factor);
            }
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   (ia == ib)) {
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                expr = mutate(max(mul_a->a, mul_b->a) * factor);
            } else {
                expr = mutate(min(mul_a->a, mul_b->a) * factor);
            }
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   const_int(mul_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia &&
                   (ib % ia == 0)) {
            // max(x*8, 24) -> max(x, 3)*8
            Expr ratio = make_const(op->type, ib / ia);
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                expr = mutate(max(mul_a->a, ratio) * factor);
            } else {
                expr = mutate(min(mul_a->a, ratio) * factor);
            }
        } else if (call_a &&
                   call_a->is_intrinsic(Call::likely) &&
                   equal(call_a->args[0], b)) {
            // max(likely(b), b) -> likely(b)
            expr = a;
        } else if (call_b &&
                   call_b->is_intrinsic(Call::likely) &&
                   equal(call_b->args[0], a)) {
            // max(a, likely(a)) -> likely(a)
            expr = b;
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   is_const(sub_a->a) &&
                   is_const(b)) {
            // max(8 - x, 3) -> 8 - min(x, 5)
            expr = mutate(sub_a->a - min(sub_a->b, sub_a->a - b));
        } else if (select_a &&
                   select_b &&
                   equal(select_a->condition, select_b->condition)) {
            expr = mutate(select(select_a->condition,
                                 max(select_a->true_value, select_b->true_value),
                                 max(select_a->false_value, select_b->false_value)));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Max::make(a, b);
        }
    }

    void visit(const EQ *op) {
        Expr delta = mutate(op->a - op->b);

        const Broadcast *broadcast = delta.as<Broadcast>();
        const Add *add = delta.as<Add>();
        const Sub *sub = delta.as<Sub>();
        const Mul *mul = delta.as<Mul>();
        const Select *sel = delta.as<Select>();

        Expr zero = make_zero(delta.type());

        if (is_zero(delta)) {
            expr = const_true(op->type.lanes());
            return;
        } else if (is_const(delta)) {
            bool t = true;
            bool f = true;
            for (int i = 0; i < delta.type().lanes(); i++) {
                Expr deltai = extract_lane(delta, i);
                if (is_zero(deltai)) {
                    f = false;
                } else {
                    t = false;
                }
            }
            if (t) {
                expr = const_true(op->type.lanes());
                return;
            } else if (f) {
                expr = const_false(op->type.lanes());
                return;
            }
        } else if (no_overflow_scalar_int(delta.type())) {
            // Attempt to disprove using modulus remainder analysis
            ModulusRemainder mod_rem = modulus_remainder(delta, alignment_info);
            if (mod_rem.remainder) {
                expr = const_false();
                return;
            }

            // Attempt to disprove using bounds analysis
            int64_t delta_min, delta_max;
            if (const_int_bounds(delta, &delta_min, &delta_max) &&
                (delta_min > 0 || delta_max < 0)) {
                expr = const_false();
                return;
            }
        }

        if (broadcast) {
            // Push broadcasts outwards
            expr = Broadcast::make(mutate(broadcast->value ==
                                          make_zero(broadcast->value.type())),
                                   broadcast->lanes);
        } else if (add && is_const(add->b)) {
            // x + const = 0 -> x = -const
            expr = (add->a == mutate(make_zero(delta.type()) - add->b));
        } else if (sub) {
            if (is_const(sub->a)) {
                // const - x == 0 -> x == const
                expr = sub->b == sub->a;
            } else if (sub->a.same_as(op->a) && sub->b.same_as(op->b)) {
                expr = op;
            } else {
                // x - y == 0 -> x == y
                expr = (sub->a == sub->b);
            }
        } else if (mul &&
                   no_overflow(mul->type)) {
            // Restrict to int32 and greater, because, e.g. 64 * 4 == 0 as a uint8.
            expr = mutate(mul->a == zero || mul->b == zero);
        } else if (sel && is_zero(sel->true_value)) {
            // select(c, 0, f) == 0 -> c || (f == 0)
            expr = mutate(sel->condition || (sel->false_value == zero));
        } else if (sel && is_const(sel->true_value)) {
            // select(c, 4, f) == 0 -> !c && (f == 0)
            expr = mutate((!sel->condition) && (sel->false_value == zero));
        } else if (sel && is_zero(sel->false_value)) {
            // select(c, t, 0) == 0 -> !c || (t == 0)
            expr = mutate((!sel->condition) || (sel->true_value == zero));
        } else if (sel && is_const(sel->false_value)) {
            // select(c, t, 4) == 0 -> c && (t == 0)
            expr = mutate((sel->condition) && (sel->true_value == zero));
        } else {
            expr = (delta == make_zero(delta.type()));
        }
    }

    void visit(const NE *op) {
        expr = mutate(Not::make(op->a == op->b));
    }

    void visit(const LT *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        int64_t a_min, a_max, b_min, b_max;
        if (const_int_bounds(a, &a_min, &a_max) &&
            const_int_bounds(b, &b_min, &b_max)) {
            if (a_max < b_min) {
                expr = const_true();
                return;
            }
            if (a_min >= b_max) {
                expr = const_false();
                return;
            }
        }

        Expr delta = mutate(a - b);

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Ramp *delta_ramp = delta.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();
        const Min *min_a = a.as<Min>();
        const Min *min_b = b.as<Min>();
        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();
        const Div *div_a_a = mul_a ? mul_a->a.as<Div>() : nullptr;
        const Add *add_a_a_a = div_a_a ? div_a_a->a.as<Add>() : nullptr;

        int64_t ia = 0, ib = 0, ic = 0;
        uint64_t ua = 0, ub = 0;

        ModulusRemainder mod_rem(0, 1);
        if (delta_ramp &&
            no_overflow_scalar_int(delta_ramp->base.type())) {
            // Do modulus remainder analysis on the base.
            mod_rem = modulus_remainder(delta_ramp->base, alignment_info);
        }

        // Note that the computation of delta could be incorrect if
        // ia and/or ib are large unsigned integer constants, especially when
        // int is 32 bits on the machine.
        // Explicit comparison is preferred.
        if (const_int(a, &ia) &&
            const_int(b, &ib)) {
            expr = make_bool(ia < ib, op->type.lanes());
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            expr = make_bool(ua < ub, op->type.lanes());
        } else if (const_int(a, &ia) &&
                   a.type().is_max(ia)) {
            // Comparing maximum of type < expression of type.  This can never be true.
            expr = const_false(op->type.lanes());
        } else if (const_int(b, &ib) &&
                   b.type().is_min(ib)) {
            // Comparing expression of type < minimum of type.  This can never be true.
            expr = const_false(op->type.lanes());
        } else if (is_zero(delta) ||
                   (no_overflow(delta.type()) &&
                    is_positive_const(delta))) {
            expr = const_false(op->type.lanes());
        } else if (no_overflow(delta.type()) &&
                   is_negative_const(delta)) {
            expr = const_true(op->type.lanes());
        } else if (broadcast_a &&
                   broadcast_b) {
            // Push broadcasts outwards
            expr = mutate(Broadcast::make(broadcast_a->value < broadcast_b->value, broadcast_a->lanes));
        } else if (no_overflow(delta.type())) {
            if (ramp_a &&
                ramp_b &&
                equal(ramp_a->stride, ramp_b->stride)) {
                // Ramps with matching stride
                Expr bases_lt = (ramp_a->base < ramp_b->base);
                expr = mutate(Broadcast::make(bases_lt, ramp_a->lanes));
            } else if (add_a &&
                       add_b &&
                       equal(add_a->a, add_b->a)) {
                // Subtract a term from both sides
                expr = mutate(add_a->b < add_b->b);
            } else if (add_a &&
                       add_b &&
                       equal(add_a->a, add_b->b)) {
                expr = mutate(add_a->b < add_b->a);
            } else if (add_a &&
                       add_b &&
                       equal(add_a->b, add_b->a)) {
                expr = mutate(add_a->a < add_b->b);
            } else if (add_a &&
                       add_b &&
                       equal(add_a->b, add_b->b)) {
                expr = mutate(add_a->a < add_b->a);
            } else if (sub_a &&
                       sub_b &&
                       equal(sub_a->a, sub_b->a)) {
                // Add a term to both sides and negate.
                expr = mutate(sub_b->b < sub_a->b);
            } else if (sub_a &&
                       sub_b &&
                       equal(sub_a->b, sub_b->b)) {
                expr = mutate(sub_a->a < sub_b->a);
            } else if (add_a) {
                // Rearrange so that all adds and subs are on the rhs to cut down on further cases
                expr = mutate(add_a->a < (b - add_a->b));
            } else if (sub_a) {
                expr = mutate(sub_a->a < (b + sub_a->b));
            } else if (add_b &&
                       equal(add_b->a, a)) {
                // Subtract a term from both sides
                expr = mutate(make_zero(add_b->b.type()) < add_b->b);
            } else if (add_b &&
                       equal(add_b->b, a)) {
                expr = mutate(make_zero(add_b->a.type()) < add_b->a);
            } else if (add_b &&
                       is_simple_const(a) &&
                       is_simple_const(add_b->b)) {
                // a < x + b -> (a - b) < x
                expr = mutate((a - add_b->b) < add_b->a);
            } else if (sub_b &&
                       equal(sub_b->a, a)) {
                // Subtract a term from both sides
                expr = mutate(sub_b->b < make_zero(sub_b->b.type()));
            } else if (sub_b &&
                       is_const(a) &&
                       is_const(sub_b->a)) {
                // (c1 < c2 - x) -> (x < c2 - c1)
                expr = mutate(sub_b->b < (sub_b->a - a));
            } else if (mul_a &&
                       mul_b &&
                       is_positive_const(mul_a->b) &&
                       is_positive_const(mul_b->b) &&
                       equal(mul_a->b, mul_b->b)) {
                // Divide both sides by a constant
                expr = mutate(mul_a->a < mul_b->a);
            } else if (mul_a &&
                       is_positive_const(mul_a->b) &&
                       is_const(b)) {
                if (mul_a->type.is_int()) {
                    // (a * c1 < c2) <=> (a < (c2 - 1) / c1 + 1)
                    expr = mutate(mul_a->a < (((b - 1) / mul_a->b) + 1));
                } else {
                    // (a * c1 < c2) <=> (a < c2 / c1)
                    expr = mutate(mul_a->a < (b / mul_a->b));
                }
            } else if (mul_b &&
                       is_positive_const(mul_b->b) &&
                       is_const(a)) {
                // (c1 < b * c2) <=> ((c1 / c2) < b)
                expr = mutate((a / mul_b->b) < mul_b->a);
            } else if (a.type().is_int() &&
                       div_a &&
                       is_positive_const(div_a->b) &&
                       is_const(b)) {
                // a / c1 < c2 <=> a < c1*c2
                expr = mutate(div_a->a < (div_a->b * b));
            } else if (a.type().is_int() &&
                       div_b &&
                       is_positive_const(div_b->b) &&
                       is_const(a)) {
                // c1 < b / c2 <=> (c1+1)*c2-1 < b
                Expr one = make_one(a.type());
                expr = mutate((a + one)*div_b->b - one < div_b->a);
            } else if (min_a) {
                // (min(a, b) < c) <=> (a < c || b < c)
                // See if that would simplify usefully:
                Expr lt_a = mutate(min_a->a < b);
                Expr lt_b = mutate(min_a->b < b);
                if (is_const(lt_a) || is_const(lt_b)) {
                    expr = mutate(lt_a || lt_b);
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    expr = op;
                } else {
                    expr = LT::make(a, b);
                }
            } else if (max_a) {
                // (max(a, b) < c) <=> (a < c && b < c)
                Expr lt_a = mutate(max_a->a < b);
                Expr lt_b = mutate(max_a->b < b);
                if (is_const(lt_a) || is_const(lt_b)) {
                    expr = mutate(lt_a && lt_b);
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    expr = op;
                } else {
                    expr = LT::make(a, b);
                }
            } else if (min_b) {
                // (a < min(b, c)) <=> (a < b && a < c)
                Expr lt_a = mutate(a < min_b->a);
                Expr lt_b = mutate(a < min_b->b);
                if (is_const(lt_a) || is_const(lt_b)) {
                    expr = mutate(lt_a && lt_b);
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    expr = op;
                } else {
                    expr = LT::make(a, b);
                }
            } else if (max_b) {
                // (a < max(b, c)) <=> (a < b || a < c)
                Expr lt_a = mutate(a < max_b->a);
                Expr lt_b = mutate(a < max_b->b);
                if (is_const(lt_a) || is_const(lt_b)) {
                    expr = mutate(lt_a || lt_b);
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    expr = op;
                } else {
                    expr = LT::make(a, b);
                }
            } else if (mul_a &&
                       div_a_a &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(div_a_a->a, b)) {
                // subtract (x/c1)*c1 from both sides
                // (x/c1)*c1 < x -> 0 < x % c1
                expr = mutate(0 < b % make_const(a.type(), ia));
            } else if (mul_a &&
                       div_a_a &&
                       add_b &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(div_a_a->a, add_b->a)) {
                // subtract (x/c1)*c1 from both sides
                // (x/c1)*c1 < x + y -> 0 < x % c1 + y
                expr = mutate(0 < add_b->a % div_a_a->b + add_b->b);
            } else if (mul_a &&
                       div_a_a &&
                       sub_b &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(div_a_a->a, sub_b->a)) {
                // subtract (x/c1)*c1 from both sides
                // (x/c1)*c1 < x - y -> y < x % c1
                expr = mutate(sub_b->b < sub_b->a % div_a_a->b);
            } else if (mul_a &&
                       div_a_a &&
                       add_a_a_a &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       const_int(add_a_a_a->b, &ic) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(add_a_a_a->a, b)) {
                // subtract ((x+c2)/c1)*c1 from both sides
                // ((x+c2)/c1)*c1 < x -> c2 < (x+c2) % c1
                expr = mutate(add_a_a_a->b < div_a_a->a % div_a_a->b);
            } else if (mul_a &&
                       div_a_a &&
                       add_b &&
                       add_a_a_a &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       const_int(add_a_a_a->b, &ic) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(add_a_a_a->a, add_b->a)) {
                // subtract ((x+c2)/c1)*c1 from both sides
                // ((x+c2)/c1)*c1 < x + y -> c2 < (x+c2) % c1 + y
                expr = mutate(add_a_a_a->b < div_a_a->a % div_a_a->b + add_b->b);
            } else if (mul_a &&
                       div_a_a &&
                       add_a_a_a &&
                       sub_b &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       const_int(add_a_a_a->b, &ic) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(add_a_a_a->a, sub_b->a)) {
                // subtract ((x+c2)/c1)*c1 from both sides
                // ((x+c2)/c1)*c1 < x - y -> y < (x+c2) % c1 + (-c2)
                expr = mutate(sub_b->b < div_a_a->a % div_a_a->b + make_const(a.type(), -ic));
            } else if (delta_ramp &&
                       is_positive_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base + delta_ramp->stride*(delta_ramp->lanes - 1) < 0))) {
                expr = const_true(delta_ramp->lanes);
            } else if (delta_ramp &&
                       is_positive_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base >= 0))) {
                expr = const_false(delta_ramp->lanes);
            } else if (delta_ramp &&
                       is_negative_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base < 0))) {
                expr = const_true(delta_ramp->lanes);
            } else if (delta_ramp &&
                       is_negative_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base + delta_ramp->stride*(delta_ramp->lanes - 1) >= 0))) {
                expr = const_false(delta_ramp->lanes);
            } else if (delta_ramp && mod_rem.modulus > 0 &&
                       const_int(delta_ramp->stride, &ia) &&
                       0 <= ia * (delta_ramp->lanes - 1) + mod_rem.remainder &&
                       ia * (delta_ramp->lanes - 1) + mod_rem.remainder < mod_rem.modulus) {
                // ramp(x, a, b) < 0 -> broadcast(x < 0, b)
                expr = Broadcast::make(mutate(LT::make(delta_ramp->base / mod_rem.modulus, 0)), delta_ramp->lanes);
            } else if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = LT::make(a, b);
            }
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = LT::make(a, b);
        }
    }

    void visit(const LE *op) {
        expr = mutate(!(op->b < op->a));
    }

    void visit(const GT *op) {
        expr = mutate(op->b < op->a);
    }

    void visit(const GE *op) {
        expr = mutate(!(op->a < op->b));
    }

    void visit(const And *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const LE *le_a = a.as<LE>();
        const LE *le_b = b.as<LE>();
        const LT *lt_a = a.as<LT>();
        const LT *lt_b = b.as<LT>();
        const EQ *eq_a = a.as<EQ>();
        const EQ *eq_b = b.as<EQ>();
        const NE *neq_a = a.as<NE>();
        const NE *neq_b = b.as<NE>();
        const Not *not_a = a.as<Not>();
        const Not *not_b = b.as<Not>();
        const Variable *var_a = a.as<Variable>();
        const Variable *var_b = b.as<Variable>();

        if (is_one(a)) {
            expr = b;
        } else if (is_one(b)) {
            expr = a;
        } else if (is_zero(a)) {
            expr = a;
        } else if (is_zero(b)) {
            expr = b;
        } else if (equal(a, b)) {
            // a && a -> a
            expr = a;
        } else if (le_a &&
                   le_b &&
                   equal(le_a->a, le_b->a)) {
            // (x <= foo && x <= bar) -> x <= min(foo, bar)
            expr = mutate(le_a->a <= min(le_a->b, le_b->b));
        } else if (le_a &&
                   le_b &&
                   equal(le_a->b, le_b->b)) {
            // (foo <= x && bar <= x) -> max(foo, bar) <= x
            expr = mutate(max(le_a->a, le_b->a) <= le_a->b);
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->a, lt_b->a)) {
            // (x < foo && x < bar) -> x < min(foo, bar)
            expr = mutate(lt_a->a < min(lt_a->b, lt_b->b));
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->b, lt_b->b)) {
            // (foo < x && bar < x) -> max(foo, bar) < x
            expr = mutate(max(lt_a->a, lt_b->a) < lt_a->b);
        } else if (eq_a &&
                   neq_b &&
                   ((equal(eq_a->a, neq_b->a) && equal(eq_a->b, neq_b->b)) ||
                    (equal(eq_a->a, neq_b->b) && equal(eq_a->b, neq_b->a)))) {
            // a == b && a != b
            expr = const_false(op->type.lanes());
        } else if (eq_b &&
                   neq_a &&
                   ((equal(eq_b->a, neq_a->a) && equal(eq_b->b, neq_a->b)) ||
                    (equal(eq_b->a, neq_a->b) && equal(eq_b->b, neq_a->a)))) {
            // a != b && a == b
            expr = const_false(op->type.lanes());
        } else if ((not_a && equal(not_a->a, b)) ||
                   (not_b && equal(not_b->a, a))) {
            // a && !a
            expr = const_false(op->type.lanes());
        } else if (le_a &&
                   lt_b &&
                   equal(le_a->a, lt_b->b) &&
                   equal(le_a->b, lt_b->a)) {
            // a <= b && b < a
            expr = const_false(op->type.lanes());
        } else if (lt_a &&
                   le_b &&
                   equal(lt_a->a, le_b->b) &&
                   equal(lt_a->b, le_b->a)) {
            // a < b && b <= a
            expr = const_false(op->type.lanes());
        } else if (broadcast_a &&
                   broadcast_b &&
                   broadcast_a->lanes == broadcast_b->lanes) {
            // x8(a) && x8(b) -> x8(a && b)
            expr = Broadcast::make(mutate(And::make(broadcast_a->value, broadcast_b->value)), broadcast_a->lanes);
        } else if (var_a && expr_uses_var(b, var_a->name)) {
            expr = mutate(a && substitute(var_a->name, make_one(a.type()), b));
        } else if (var_b && expr_uses_var(a, var_b->name)) {
            expr = mutate(substitute(var_b->name, make_one(b.type()), a) && b);
        } else if (a.same_as(op->a) &&
                   b.same_as(op->b)) {
            expr = op;
        } else {
            expr = And::make(a, b);
        }
    }

    void visit(const Or *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const EQ *eq_a = a.as<EQ>();
        const EQ *eq_b = b.as<EQ>();
        const NE *neq_a = a.as<NE>();
        const NE *neq_b = b.as<NE>();
        const Not *not_a = a.as<Not>();
        const Not *not_b = b.as<Not>();
        const LE *le_a = a.as<LE>();
        const LE *le_b = b.as<LE>();
        const LT *lt_a = a.as<LT>();
        const LT *lt_b = b.as<LT>();
        const Variable *var_a = a.as<Variable>();
        const Variable *var_b = b.as<Variable>();

        if (is_one(a)) {
            expr = a;
        } else if (is_one(b)) {
            expr = b;
        } else if (is_zero(a)) {
            expr = b;
        } else if (is_zero(b)) {
            expr = a;
        } else if (equal(a, b)) {
            expr = a;
        } else if (eq_a &&
                   neq_b &&
                   ((equal(eq_a->a, neq_b->a) && equal(eq_a->b, neq_b->b)) ||
                    (equal(eq_a->a, neq_b->b) && equal(eq_a->b, neq_b->a)))) {
            // a == b || a != b
            expr = const_true(op->type.lanes());
        } else if (neq_a &&
                   eq_b &&
                   ((equal(eq_b->a, neq_a->a) && equal(eq_b->b, neq_a->b)) ||
                    (equal(eq_b->a, neq_a->b) && equal(eq_b->b, neq_a->a)))) {
            // a != b || a == b
            expr = const_true(op->type.lanes());
        } else if ((not_a && equal(not_a->a, b)) ||
                   (not_b && equal(not_b->a, a))) {
            // a || !a
            expr = const_true(op->type.lanes());
        } else if (le_a &&
                   lt_b &&
                   equal(le_a->a, lt_b->b) &&
                   equal(le_a->b, lt_b->a)) {
            // a <= b || b < a
            expr = const_true(op->type.lanes());
        } else if (lt_a &&
                   le_b &&
                   equal(lt_a->a, le_b->b) &&
                   equal(lt_a->b, le_b->a)) {
            // a < b || b <= a
            expr = const_true(op->type.lanes());
        } else if (broadcast_a &&
                   broadcast_b &&
                   broadcast_a->lanes == broadcast_b->lanes) {
            // x8(a) || x8(b) -> x8(a || b)
            expr = Broadcast::make(mutate(Or::make(broadcast_a->value, broadcast_b->value)), broadcast_a->lanes);
        } else if (var_a && expr_uses_var(b, var_a->name)) {
            expr = mutate(a || substitute(var_a->name, make_zero(a.type()), b));
        } else if (var_b && expr_uses_var(a, var_b->name)) {
            expr = mutate(substitute(var_b->name, make_zero(b.type()), a) || b);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Or::make(a, b);
        }
    }

    void visit(const Not *op) {
        Expr a = mutate(op->a);

        if (is_one(a)) {
            expr = make_zero(a.type());
        } else if (is_zero(a)) {
            expr = make_one(a.type());
        } else if (const Not *n = a.as<Not>()) {
            // Double negatives cancel
            expr = n->a;
        } else if (const LE *n = a.as<LE>()) {
            expr = LT::make(n->b, n->a);
        } else if (const GE *n = a.as<GE>()) {
            expr = LT::make(n->a, n->b);
        } else if (const LT *n = a.as<LT>()) {
            expr = LE::make(n->b, n->a);
        } else if (const GT *n = a.as<GT>()) {
            expr = LE::make(n->a, n->b);
        } else if (const NE *n = a.as<NE>()) {
            expr = EQ::make(n->a, n->b);
        } else if (const EQ *n = a.as<EQ>()) {
            expr = NE::make(n->a, n->b);
        } else if (const Broadcast *n = a.as<Broadcast>()) {
            expr = mutate(Broadcast::make(!n->value, n->lanes));
        } else if (a.same_as(op->a)) {
            expr = op;
        } else {
            expr = Not::make(a);
        }
    }

    void visit(const Select *op) {
        Expr condition = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);

        const Call *ct = true_value.as<Call>();
        const Call *cf = false_value.as<Call>();
        const Select *sel_t = true_value.as<Select>();
        const Select *sel_f = false_value.as<Select>();
        const Add *add_t = true_value.as<Add>();
        const Add *add_f = false_value.as<Add>();
        const Sub *sub_t = true_value.as<Sub>();
        const Sub *sub_f = false_value.as<Sub>();
        const Mul *mul_t = true_value.as<Mul>();
        const Mul *mul_f = false_value.as<Mul>();

        if (is_zero(condition)) {
            expr = false_value;
        } else if (is_one(condition)) {
            expr = true_value;
        } else if (equal(true_value, false_value)) {
            expr = true_value;
        } else if (true_value.type().is_bool() &&
                   is_one(true_value) &&
                   is_zero(false_value)) {
            if (true_value.type().is_vector() && condition.type().is_scalar()) {
                expr = Broadcast::make(condition, true_value.type().lanes());
            } else {
                expr = condition;
            }
        } else if (true_value.type().is_bool() &&
                   is_zero(true_value) &&
                   is_one(false_value)) {
            if (true_value.type().is_vector() && condition.type().is_scalar()) {
                expr = Broadcast::make(mutate(!condition), true_value.type().lanes());
            } else {
                expr = mutate(!condition);
            }
        } else if (const Broadcast *b = condition.as<Broadcast>()) {
            // Select of broadcast -> scalar select
            expr = mutate(Select::make(b->value, true_value, false_value));
        } else if (const NE *ne = condition.as<NE>()) {
            // Normalize select(a != b, c, d) to select(a == b, d, c)
            expr = mutate(Select::make(ne->a == ne->b, false_value, true_value));
        } else if (const LE *le = condition.as<LE>()) {
            // Normalize select(a <= b, c, d) to select(b < a, d, c)
            expr = mutate(Select::make(le->b < le->a, false_value, true_value));
        } else if (ct && ct->is_intrinsic(Call::likely) &&
                   equal(ct->args[0], false_value)) {
            // select(cond, likely(a), a) -> likely(a)
            expr = true_value;
        } else if (cf &&
                   cf->is_intrinsic(Call::likely) &&
                   equal(cf->args[0], true_value)) {
            // select(cond, a, likely(a)) -> likely(a)
            expr = false_value;
        } else if (sel_t &&
                   equal(sel_t->true_value, false_value)) {
            // select(a, select(b, c, d), c) -> select(a && !b, d, c)
            expr = mutate(Select::make(condition && !sel_t->condition, sel_t->false_value, false_value));
        } else if (sel_t &&
                   equal(sel_t->false_value, false_value)) {
            // select(a, select(b, c, d), d) -> select(a && b, c, d)
            expr = mutate(Select::make(condition && sel_t->condition, sel_t->true_value, false_value));
        } else if (sel_f &&
                   equal(sel_f->false_value, true_value)) {
            // select(a, d, select(b, c, d)) -> select(a || !b, d, c)
            expr = mutate(Select::make(condition || !sel_f->condition, true_value, sel_f->true_value));
        } else if (sel_f &&
                   equal(sel_f->true_value, true_value)) {
            // select(a, d, select(b, d, c)) -> select(a || b, d, c)
            expr = mutate(Select::make(condition || sel_f->condition, true_value, sel_f->false_value));
        } else if (add_t &&
                   add_f &&
                   equal(add_t->a, add_f->a)) {
            // select(c, a+b, a+d) -> a + select(x, b, d)
            expr = mutate(add_t->a + Select::make(condition, add_t->b, add_f->b));
        } else if (add_t &&
                   add_f &&
                   equal(add_t->a, add_f->b)) {
            // select(c, a+b, d+a) -> a + select(x, b, d)
            expr = mutate(add_t->a + Select::make(condition, add_t->b, add_f->a));
        } else if (add_t &&
                   add_f &&
                   equal(add_t->b, add_f->a)) {
            // select(c, b+a, a+d) -> a + select(x, b, d)
            expr = mutate(add_t->b + Select::make(condition, add_t->a, add_f->b));
        } else if (add_t &&
                   add_f &&
                   equal(add_t->b, add_f->b)) {
            // select(c, b+a, d+a) -> select(x, b, d) + a
            expr = mutate(Select::make(condition, add_t->a, add_f->a) + add_t->b);
        } else if (sub_t &&
                   sub_f &&
                   equal(sub_t->a, sub_f->a)) {
            // select(c, a-b, a-d) -> a - select(x, b, d)
            expr = mutate(sub_t->a - Select::make(condition, sub_t->b, sub_f->b));
        } else if (sub_t &&
                   sub_f &&
                   equal(sub_t->b, sub_f->b)) {
            // select(c, b-a, d-a) -> select(x, b, d) - a
            expr = mutate(Select::make(condition, sub_t->a, sub_f->a) - sub_t->b);\
        } else if (add_t &&
                   sub_f &&
                   equal(add_t->a, sub_f->a)) {
            // select(c, a+b, a-d) -> a + select(x, b, 0-d)
            expr = mutate(add_t->a + Select::make(condition, add_t->b, make_zero(sub_f->b.type()) - sub_f->b));
        } else if (add_t &&
                   sub_f &&
                   equal(add_t->b, sub_f->a)) {
            // select(c, b+a, a-d) -> a + select(x, b, 0-d)
            expr = mutate(add_t->b + Select::make(condition, add_t->a, make_zero(sub_f->b.type()) - sub_f->b));
        } else if (sub_t &&
                   add_f &&
                   equal(sub_t->a, add_f->a)) {
            // select(c, a-b, a+d) -> a + select(x, 0-b, d)
            expr = mutate(sub_t->a + Select::make(condition, make_zero(sub_t->b.type()) - sub_t->b, add_f->b));
        } else if (sub_t &&
                   add_f &&
                   equal(sub_t->a, add_f->b)) {
            // select(c, a-b, d+a) -> a + select(x, 0-b, d)
            expr = mutate(sub_t->a + Select::make(condition, make_zero(sub_t->b.type()) - sub_t->b, add_f->a));
        } else if (mul_t &&
                   mul_f &&
                   equal(mul_t->a, mul_f->a)) {
            // select(c, a*b, a*d) -> a * select(x, b, d)
            expr = mutate(mul_t->a * Select::make(condition, mul_t->b, mul_f->b));
        } else if (mul_t &&
                   mul_f &&
                   equal(mul_t->a, mul_f->b)) {
            // select(c, a*b, d*a) -> a * select(x, b, d)
            expr = mutate(mul_t->a * Select::make(condition, mul_t->b, mul_f->a));
        } else if (mul_t &&
                   mul_f &&
                   equal(mul_t->b, mul_f->a)) {
            // select(c, b*a, a*d) -> a * select(x, b, d)
            expr = mutate(mul_t->b * Select::make(condition, mul_t->a, mul_f->b));
        } else if (mul_t &&
                   mul_f &&
                   equal(mul_t->b, mul_f->b)) {
            // select(c, b*a, d*a) -> select(x, b, d) * a
            expr = mutate(Select::make(condition, mul_t->a, mul_f->a) * mul_t->b);
        } else if (condition.same_as(op->condition) &&
                   true_value.same_as(op->true_value) &&
                   false_value.same_as(op->false_value)) {
            expr = op;
        } else {
            expr = Select::make(condition, true_value, false_value);
        }
    }

    void visit(const Ramp *op) {
        Expr base = mutate(op->base);
        Expr stride = mutate(op->stride);

        if (is_zero(stride)) {
            expr = Broadcast::make(base, op->lanes);
        } else if (base.same_as(op->base) &&
                   stride.same_as(op->stride)) {
            expr = op;
        } else {
            expr = Ramp::make(base, stride, op->lanes);
        }
    }

    void visit(const IfThenElse *op) {
        Expr condition = mutate(op->condition);

        // If (true) ...
        if (is_one(condition)) {
            stmt = mutate(op->then_case);
            return;
        }

        // If (false) ...
        if (is_zero(condition)) {
            stmt = mutate(op->else_case);
            if (!stmt.defined()) {
                // Emit a noop
                stmt = Evaluate::make(0);
            }
            return;
        }

        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        // If both sides are no-ops, bail out.
        if (is_no_op(then_case) && is_no_op(else_case)) {
            stmt = then_case;
            return;
        }

        // Remember the statements before substitution.
        Stmt then_nosubs = then_case;
        Stmt else_nosubs = else_case;

        // Mine the condition for useful constraints to apply (eg var == value && bool_param).
        vector<Expr> stack;
        stack.push_back(condition);
        bool and_chain = false, or_chain = false;
        while (!stack.empty()) {
            Expr next = stack.back();
            stack.pop_back();

            if (!or_chain) {
                then_case = substitute(next, const_true(), then_case);
            }
            if (!and_chain) {
                else_case = substitute(next, const_false(), else_case);
            }

            if (const And *a = next.as<And>()) {
                if (!or_chain) {
                    stack.push_back(a->b);
                    stack.push_back(a->a);
                    and_chain = true;
                }
            } else if (const Or *o = next.as<Or>()) {
                if (!and_chain) {
                    stack.push_back(o->b);
                    stack.push_back(o->a);
                    or_chain = true;
                }
            } else {
                const EQ *eq = next.as<EQ>();
                const NE *ne = next.as<NE>();
                const Variable *var = eq ? eq->a.as<Variable>() : next.as<Variable>();

                if (eq && var) {
                    if (!or_chain) {
                        then_case = substitute(var->name, eq->b, then_case);
                    }
                    if (!and_chain && eq->b.type().is_bool()) {
                        else_case = substitute(var->name, !eq->b, then_case);
                    }
                } else if (var) {
                    if (!or_chain) {
                        then_case = substitute(var->name, const_true(), then_case);
                    }
                    if (!and_chain) {
                        else_case = substitute(var->name, const_false(), else_case);
                    }
                } else if (eq && is_const(eq->b) && !or_chain) {
                    // some_expr = const
                    then_case = substitute(eq->a, eq->b, then_case);
                } else if (ne && is_const(ne->b) && !and_chain) {
                    // some_expr != const
                    else_case = substitute(ne->a, ne->b, else_case);
                }
            }
        }

        // If substitutions have been made, simplify again.
        if (!then_case.same_as(then_nosubs)) {
            then_case = mutate(then_case);
        }
        if (!else_case.same_as(else_nosubs)) {
            else_case = mutate(else_case);
        }

        if (condition.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            stmt = op;
        } else {
            stmt = IfThenElse::make(condition, then_case, else_case);
        }
    }

    void visit(const Load *op) {
        // Load of a broadcast should be broadcast of the load
        Expr index = mutate(op->index);
        if (const Broadcast *b = index.as<Broadcast>()) {
            Expr load = Load::make(op->type.element_of(), op->name, b->value, op->image, op->param);
            expr = Broadcast::make(load, b->lanes);
        } else if (index.same_as(op->index)) {
            expr = op;
        } else {
            expr = Load::make(op->type, op->name, index, op->image, op->param);
        }
    }

    void visit(const Call *op) {
        // Calls implicitly depend on mins and strides of the buffer referenced
        if (op->call_type == Call::Image || op->call_type == Call::Halide) {
            for (size_t i = 0; i < op->args.size(); i++) {
                {
                    ostringstream oss;
                    oss << op->name << ".stride." << i;
                    string stride = oss.str();
                    if (var_info.contains(stride)) {
                        var_info.ref(stride).old_uses++;
                    }
                }
                {
                    ostringstream oss;
                    oss << op->name << ".min." << i;
                    string min = oss.str();
                    if (var_info.contains(min)) {
                        var_info.ref(min).old_uses++;
                    }
                }
            }
        }

        if (op->is_intrinsic(Call::shift_left) ||
            op->is_intrinsic(Call::shift_right)) {
            Expr a = mutate(op->args[0]), b = mutate(op->args[1]);

            int64_t ib = 0;
            if (const_int(b, &ib) || const_uint(b, (uint64_t *)(&ib))) {
                Type t = op->type;

                bool shift_left = op->is_intrinsic(Call::shift_left);
                if (t.is_int() && ib < 0) {
                    shift_left = !shift_left;
                    ib = -ib;
                }

                if (ib >= 0 && ib < std::min(t.bits(), 64) - 1) {
                    ib = 1LL << ib;
                    b = make_const(t, ib);

                    if (shift_left) {
                        expr = mutate(Mul::make(a, b));
                    } else {
                        expr = mutate(Div::make(a, b));
                    }
                    return;
                } else {
                    user_warning << "Cannot replace bit shift with arithmetic "
                                 << "operator (integer overflow).\n";
                }
            }

            if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
                expr = op;
            } else if (op->is_intrinsic(Call::shift_left)) {
                expr = a << b;
            } else {
                expr = a >> b;
            }
        } else if (op->is_intrinsic(Call::bitwise_and)) {
            Expr a = mutate(op->args[0]), b = mutate(op->args[1]);
            int64_t ib = 0;
            uint64_t ub = 0;
            int bits;

            if (const_int(b, &ib) &&
                !b.type().is_max(ib) &&
                is_const_power_of_two_integer(make_const(a.type(), ib + 1), &bits)) {
                expr = Mod::make(a, make_const(a.type(), ib + 1));
            } else if (const_uint(b, &ub)) {
                if (b.type().is_max(ub)) {
                    expr = a;
                } else if (is_const_power_of_two_integer(make_const(a.type(), ub + 1), &bits)) {
                    expr = Mod::make(a, make_const(a.type(), ub + 1));
                }
            } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
                expr = op;
            } else {
                expr = a & b;
            }
        } else if (op->is_intrinsic(Call::abs)) {
            // Constant evaluate abs(x).
            Expr a = mutate(op->args[0]);
            Type ta = a.type();
            int64_t ia = 0;
            double fa = 0;
            if (ta.is_int() && const_int(a, &ia)) {
                if (ia < 0 && !(Int(64).is_min(ia))) {
                    ia = -ia;
                }
                expr = make_const(op->type, ia);
            } else if (ta.is_uint()) {
                // abs(uint) is a no-op.
                expr = a;
            } else if (const_float(a, &fa)) {
                if (fa < 0) {
                    fa = -fa;
                }
                expr = make_const(a.type(), fa);
            } else if (a.same_as(op->args[0])) {
                expr = op;
            } else {
                expr = abs(a);
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "is_nan_f32") {
            Expr arg = mutate(op->args[0]);
            double f = 0.0;
            if (const_float(arg, &f)) {
                expr = std::isnan(f);
            } else if (arg.same_as(op->args[0])) {
                expr = op;
            } else {
                expr = Call::make(op->type, op->name, {arg}, op->call_type);
            }
        } else if (op->is_intrinsic(Call::interleave_vectors)) {
            // Mutate the args
            vector<Expr> new_args;
            bool changed = false;
            for (Expr arg : op->args) {
                Expr new_arg = mutate(arg);
                if (!arg.same_as(new_arg)) {
                    changed = true;
                }
                new_args.push_back(new_arg);
            }
            int terms = (int)new_args.size();

            // Try to collapse an interleave of ramps into a single ramp.
            const Ramp *r = new_args[0].as<Ramp>();
            if (r) {
                bool can_collapse = true;
                for (size_t i = 1; i < new_args.size(); i++) {
                    // If we collapse these terms into a single ramp,
                    // the new stride is going to be the old stride
                    // divided by the number of terms, so the
                    // difference between two adjacent terms in the
                    // interleave needs to be a broadcast of the new
                    // stride.
                    Expr diff = mutate(new_args[i] - new_args[i-1]);
                    const Broadcast *b = diff.as<Broadcast>();
                    if (b) {
                        Expr check = mutate(b->value * terms - r->stride);
                        can_collapse &= is_zero(check);
                    } else {
                        can_collapse = false;
                    }
                }
                if (can_collapse) {
                    expr = Ramp::make(r->base, mutate(r->stride / terms), r->lanes * terms);
                    return;
                }
            }

            // Try to collapse an interleave of strided loads of ramps
            // from the same buffer into a single load of a ramp.
            if (const Load *first_load = new_args[0].as<Load>()) {
                vector<Expr> load_indices;
                for (Expr e : new_args) {
                    const Load *load = e.as<Load>();
                    if (load && load->name == first_load->name) {
                        load_indices.push_back(load->index);
                    }
                }

                if ((int)load_indices.size() == terms) {
                    Type t = load_indices[0].type().with_lanes(load_indices[0].type().lanes() * terms);
                    Expr interleaved_index = Call::make(t, Call::interleave_vectors, load_indices, Call::PureIntrinsic);
                    interleaved_index = mutate(interleaved_index);
                    if (interleaved_index.as<Ramp>()) {
                        t = first_load->type;
                        t = t.with_lanes(t.lanes() * terms);
                        expr = Load::make(t, first_load->name, interleaved_index, first_load->image, first_load->param);
                        return;
                    }
                }
            }

            if (!changed) {
                expr = op;
            } else {
                expr = Call::make(op->type, op->name, new_args, op->call_type);
            }
        } else if (op->is_intrinsic(Call::stringify)) {
            // Eagerly concat constant arguments to a stringify.
            bool changed = false;
            vector<Expr> new_args;
            const StringImm *last = nullptr;
            for (size_t i = 0; i < op->args.size(); i++) {
                Expr arg = mutate(op->args[i]);
                if (!arg.same_as(op->args[i])) {
                    changed = true;
                }
                const StringImm *string_imm = arg.as<StringImm>();
                const IntImm    *int_imm    = arg.as<IntImm>();
                const FloatImm  *float_imm  = arg.as<FloatImm>();
                // We use snprintf here rather than stringstreams,
                // because the runtime's float printing is guaranteed
                // to match snprintf.
                char buf[64]; // Large enough to hold the biggest float literal.
                if (last && string_imm) {
                    new_args.back() = last->value + string_imm->value;
                    changed = true;
                } else if (int_imm) {
                    snprintf(buf, sizeof(buf), "%lld", (long long)int_imm->value);
                    if (last) {
                        new_args.back() = last->value + buf;
                    } else {
                        new_args.push_back(string(buf));
                    }
                    changed = true;
                } else if (last && float_imm) {
                    snprintf(buf, sizeof(buf), "%f", float_imm->value);
                    if (last) {
                        new_args.back() = last->value + buf;
                    } else {
                        new_args.push_back(string(buf));
                    }
                    changed = true;
                } else {
                    new_args.push_back(arg);
                }
                last = new_args.back().as<StringImm>();
            }

            if (new_args.size() == 1 && new_args[0].as<StringImm>()) {
                // stringify of a string constant is just the string constant
                expr = new_args[0];
            } else if (changed) {
                expr = Call::make(op->type, op->name, new_args, op->call_type);
            } else {
                expr = op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "log_f32") {
            Expr arg = mutate(op->args[0]);
            if (const double *f = as_const_float(arg)) {
                expr = FloatImm::make(arg.type(), std::log(*f));
            } else if (!arg.same_as(op->args[0])) {
                expr = Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                expr = op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "exp_f32") {
            Expr arg = mutate(op->args[0]);
            if (const double *f = as_const_float(arg)) {
                expr = FloatImm::make(arg.type(), std::exp(*f));
            } else if (!arg.same_as(op->args[0])) {
                expr = Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                expr = op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   (op->name == "floor_f32" || op->name == "ceil_f32" ||
                    op->name == "round_f32" || op->name == "trunc_f32")) {
            internal_assert(op->args.size() == 1);
            Expr arg = mutate(op->args[0]);
            const Call *call = arg.as<Call>();
            if (const double *f = as_const_float(arg)) {
                if (op->name == "floor_f32") {
                    expr = FloatImm::make(arg.type(), std::floor(*f));
                } else if (op->name == "ceil_f32") {
                    expr = FloatImm::make(arg.type(), std::ceil(*f));
                } else if (op->name == "round_f32") {
                    expr = FloatImm::make(arg.type(), std::nearbyint(*f));
                } else if (op->name == "trunc_f32") {
                    expr = FloatImm::make(arg.type(), (*f < 0 ? std::ceil(*f) : std::floor(*f)));
                }
            } else if (call && call->call_type == Call::PureExtern &&
                       (call->name == "floor_f32" || call->name == "ceil_f32" ||
                        call->name == "round_f32" || call->name == "trunc_f32")) {
                // For any combination of these integer-valued functions, we can
                // discard the outer function. For example, floor(ceil(x)) == ceil(x).
                expr = call;
            } else if (!arg.same_as(op->args[0])) {
                expr = Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                expr = op;
            }
        } else {
            IRMutator::visit(op);
        }
    }

    template<typename T, typename Body>
    Body simplify_let(const T *op) {
        internal_assert(!var_info.contains(op->name))
            << "Simplify only works on code where every name is unique. Repeated name: " << op->name << "\n";

        // If the value is trivial, make a note of it in the scope so
        // we can subs it in later
        Expr value = mutate(op->value);
        Body body = op->body;

        // Iteratively peel off certain operations from the let value and push them inside.
        Expr new_value = value;
        string new_name = op->name + ".s";
        Expr new_var = Variable::make(new_value.type(), new_name);
        Expr replacement = new_var;

        debug(4) << "simplify let " << op->name << " = " << value << " in ... " << op->name << " ...\n";

        while (1) {
            const Variable *var = new_value.as<Variable>();
            const Add *add = new_value.as<Add>();
            const Sub *sub = new_value.as<Sub>();
            const Mul *mul = new_value.as<Mul>();
            const Div *div = new_value.as<Div>();
            const Mod *mod = new_value.as<Mod>();
            const Ramp *ramp = new_value.as<Ramp>();
            const Cast *cast = new_value.as<Cast>();
            const LE *le = new_value.as<LE>();
            const LT *lt = new_value.as<LT>();
            const Broadcast *broadcast = new_value.as<Broadcast>();

            const Variable *var_b = nullptr;
            if (add) {
                var_b = add->b.as<Variable>();
            } else if (sub) {
                var_b = sub->b.as<Variable>();
            } else if (mul) {
                var_b = mul->b.as<Variable>();
            }

            if (is_const(new_value)) {
                replacement = substitute(new_name, new_value, replacement);
                new_value = Expr();
                break;
            } else if (var) {
                replacement = substitute(new_name, var, replacement);
                new_value = Expr();
                break;
            } else if (add && (is_const(add->b) || var_b)) {
                replacement = substitute(new_name, Add::make(new_var, add->b), replacement);
                new_value = add->a;
            } else if (mul && (is_const(mul->b) || var_b)) {
                replacement = substitute(new_name, Mul::make(new_var, mul->b), replacement);
                new_value = mul->a;
            } else if (div && is_const(div->b)) {
                replacement = substitute(new_name, Div::make(new_var, div->b), replacement);
                new_value = div->a;
            } else if (sub && (is_const(sub->b) || var_b)) {
                replacement = substitute(new_name, Sub::make(new_var, sub->b), replacement);
                new_value = sub->a;
            } else if (mod && is_const(mod->b)) {
                replacement = substitute(new_name, Mod::make(new_var, mod->b), replacement);
                new_value = mod->a;
            } else if (false && le && is_const(le->a)) {
                new_value = le->b;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, LE::make(le->a, new_var), replacement);
            } else if (false && le && is_const(le->b)) {
                new_value = le->a;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, LE::make(new_var, le->b), replacement);
            } else if (false && lt && is_const(lt->a)) {
                new_value = lt->b;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, LT::make(lt->a, new_var), replacement);
            } else if (false && lt && is_const(lt->b)) {
                new_value = lt->a;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, LT::make(new_var, lt->b), replacement);
            } else if (ramp && is_const(ramp->stride)) {
                new_value = ramp->base;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, Ramp::make(new_var, ramp->stride, ramp->lanes), replacement);
            } else if (broadcast) {
                new_value = broadcast->value;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, Broadcast::make(new_var, broadcast->lanes), replacement);
            } else if (cast && cast->type.bits() > cast->value.type().bits()) {
                // Widening casts get pushed inwards, narrowing casts
                // stay outside. This keeps the temporaries small, and
                // helps with peephole optimizations in codegen that
                // skip the widening entirely.
                new_value = cast->value;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, Cast::make(cast->type, new_var), replacement);
            } else {
                break;
            }
        }

        if (new_value.same_as(value)) {
            // Nothing to substitute
            new_value = Expr();
            replacement = Expr();
        } else {
            debug(4) << "new let " << new_name << " = " << new_value << " in ... " << replacement << " ...\n";
        }

        VarInfo info;
        info.old_uses = 0;
        info.new_uses = 0;
        info.replacement = replacement;

        var_info.push(op->name, info);

        // Before we enter the body, track the alignment info
        bool new_value_alignment_tracked = false, new_value_bounds_tracked = false;
        if (new_value.defined() && no_overflow_scalar_int(new_value.type())) {
            ModulusRemainder mod_rem = modulus_remainder(new_value, alignment_info);
            if (mod_rem.modulus > 1) {
                alignment_info.push(new_name, mod_rem);
                new_value_alignment_tracked = true;
            }
            int64_t val_min, val_max;
            if (const_int_bounds(new_value, &val_min, &val_max)) {
                bounds_info.push(new_name, make_pair(val_min, val_max));
                new_value_bounds_tracked = true;
            }
        }
        bool value_alignment_tracked = false, value_bounds_tracked = false;;
        if (no_overflow_scalar_int(value.type())) {
            ModulusRemainder mod_rem = modulus_remainder(value, alignment_info);
            if (mod_rem.modulus > 1) {
                alignment_info.push(op->name, mod_rem);
                value_alignment_tracked = true;
            }
            int64_t val_min, val_max;
            if (const_int_bounds(value, &val_min, &val_max)) {
                bounds_info.push(op->name, make_pair(val_min, val_max));
                value_bounds_tracked = true;
            }
        }

        body = mutate(body);

        if (value_alignment_tracked) {
            alignment_info.pop(op->name);
        }
        if (value_bounds_tracked) {
            bounds_info.pop(op->name);
        }
        if (new_value_alignment_tracked) {
            alignment_info.pop(new_name);
        }
        if (new_value_bounds_tracked) {
            bounds_info.pop(new_name);
        }

        info = var_info.get(op->name);
        var_info.pop(op->name);

        Body result = body;

        if (new_value.defined() && info.new_uses > 0) {
            // The new name/value may be used
            result = T::make(new_name, new_value, result);
        }

        if (info.old_uses > 0) {
            // The old name is still in use. We'd better keep it as well.
            result = T::make(op->name, value, result);
        }

        // Don't needlessly make a new Let/LetStmt node.  (Here's a
        // piece of template syntax I've never needed before).
        const T *new_op = result.template as<T>();
        if (new_op &&
            new_op->name == op->name &&
            new_op->body.same_as(op->body) &&
            new_op->value.same_as(op->value)) {
            return op;
        }

        return result;

    }


    void visit(const Let *op) {
        if (simplify_lets) {
            expr = simplify_let<Let, Expr>(op);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        if (simplify_lets) {
            stmt = simplify_let<LetStmt, Stmt>(op);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const AssertStmt *op) {
        IRMutator::visit(op);

        const AssertStmt *a = stmt.as<AssertStmt>();
        if (a && is_zero(a->condition)) {
            user_warning << "This pipeline is guaranteed to fail an assertion at runtime: \n"
                         << stmt << "\n";
        } else if (a && is_one(a->condition)) {
            stmt = Evaluate::make(0);
        }
    }


    void visit(const For *op) {
        Expr new_min = mutate(op->min);
        Expr new_extent = mutate(op->extent);

        int64_t new_min_int, new_extent_int;
        bool bounds_tracked = false;
        if (const_int(new_min, &new_min_int) &&
            const_int(new_extent, &new_extent_int)) {
            bounds_tracked = true;
            int64_t new_max_int = new_min_int + new_extent_int - 1;
            bounds_info.push(op->name, make_pair(new_min_int, new_max_int));
        }

        Stmt new_body = mutate(op->body);

        if (bounds_tracked) {
            bounds_info.pop(op->name);
        }

        if (is_no_op(new_body)) {
            stmt = new_body;
        } else if (op->min.same_as(new_min) &&
            op->extent.same_as(new_extent) &&
            op->body.same_as(new_body)) {
            stmt = op;
        } else {
            stmt = For::make(op->name, new_min, new_extent, op->for_type, op->device_api, new_body);
        }
    }

    void visit(const Provide *op) {
        // Provides implicitly depend on mins and strides of the buffer referenced
        for (size_t i = 0; i < op->args.size(); i++) {
            {
                ostringstream oss;
                oss << op->name << ".stride." << i;
                string stride = oss.str();
                if (var_info.contains(stride)) {
                    var_info.ref(stride).old_uses++;
                }
            }
            {
                ostringstream oss;
                oss << op->name << ".min." << i;
                string min = oss.str();
                if (var_info.contains(min)) {
                    var_info.ref(min).old_uses++;
                }
            }
        }

        IRMutator::visit(op);
    }

    void visit(const Store *op) {
        Expr value = mutate(op->value);
        Expr index = mutate(op->index);

        const Load *load = value.as<Load>();

        if (load && load->name == op->name && equal(load->index, index)) {
            // foo[x] = foo[x] is a no-op
            stmt = Evaluate::make(0);
        } else if (value.same_as(op->value) && index.same_as(op->index)) {
            stmt = op;
        } else {
            stmt = Store::make(op->name, value, index, op->param);
        }
    }

    void visit(const Allocate *op) {
        std::vector<Expr> new_extents;
        bool all_extents_unmodified = true;
        for (size_t i = 0; i < op->extents.size(); i++) {
            new_extents.push_back(mutate(op->extents[i]));
            all_extents_unmodified &= new_extents[i].same_as(op->extents[i]);
        }
        Stmt body = mutate(op->body);
        Expr condition = mutate(op->condition);
        Expr new_expr;
        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr);
        }
        const IfThenElse *body_if = body.as<IfThenElse>();
        if (body_if &&
            op->condition.defined() &&
            equal(op->condition, body_if->condition)) {
            // We can move the allocation into the if body case. The
            // else case must not use it.
            stmt = Allocate::make(op->name, op->type, new_extents,
                                  condition, body_if->then_case,
                                  new_expr, op->free_function);
            stmt = IfThenElse::make(body_if->condition, stmt, body_if->else_case);
        } else if (all_extents_unmodified &&
                   body.same_as(op->body) &&
                   condition.same_as(op->condition) &&
                   new_expr.same_as(op->new_expr)) {
            stmt = op;
        } else {
            stmt = Allocate::make(op->name, op->type, new_extents,
                                  condition, body,
                                  new_expr, op->free_function);
        }
    }


    void visit(const ProducerConsumer *op) {
        Stmt produce = mutate(op->produce);
        Stmt update = op->update;
        if (update.defined()) {
            update = mutate(update);
            if (is_no_op(update)) {
                update = Stmt();
            }
        }
        Stmt consume = mutate(op->consume);

        const IfThenElse *produce_if = produce.as<IfThenElse>();
        const IfThenElse *update_if  = update.as<IfThenElse>();
        const IfThenElse *consume_if = consume.as<IfThenElse>();

        if (is_no_op(produce) &&
            is_no_op(consume) &&
            is_no_op(update)) {
            stmt = Evaluate::make(0);
        } else if (produce_if &&
                   !produce_if->else_case.defined() &&
                   consume_if &&
                   !consume_if->else_case.defined() &&
                   equal(produce_if->condition, consume_if->condition) &&
                   (!update.defined() ||
                    (update_if &&
                     !update_if->else_case.defined() &&
                     equal(produce_if->condition, update_if->condition)))) {
            // All parts are guarded by the same condition. Lift it outwards.
            Expr condition = produce_if->condition;
            produce = produce_if->then_case;
            if (update_if) update = update_if->then_case;
            consume = consume_if->then_case;
            stmt = ProducerConsumer::make(op->name, produce, update, consume);
            stmt = IfThenElse::make(condition, stmt);
        } else if (produce.same_as(op->produce) &&
                   update.same_as(op->update) &&
                   consume.same_as(op->consume)) {
            stmt = op;
        } else {
            stmt = ProducerConsumer::make(op->name, produce, update, consume);
        }
    }

    void visit(const Block *op) {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);

        // Check if both halves start with a let statement.
        const LetStmt *let_first = first.as<LetStmt>();
        const LetStmt *let_rest = rest.as<LetStmt>();
        const IfThenElse *if_first = first.as<IfThenElse>();
        const IfThenElse *if_rest = rest.as<IfThenElse>();

        // Check if first is a no-op.
        if (is_no_op(first)) {
            stmt = rest;
        } else if (is_no_op(rest)) {
            stmt = first;
        } else if (let_first &&
                   let_rest &&
                   equal(let_first->value, let_rest->value)) {

            // Do both first and rest start with the same let statement (occurs when unrolling).
            Stmt new_block = mutate(Block::make(let_first->body, let_rest->body));

            // We're just going to use the first name, so if the
            // second name is different we need to rewrite it.
            if (let_rest->name != let_first->name) {
                new_block = substitute(let_rest->name,
                                       Variable::make(let_first->value.type(), let_first->name),
                                       new_block);
            }

            stmt = LetStmt::make(let_first->name, let_first->value, new_block);
        } else if (if_first &&
                   if_rest &&
                   equal(if_first->condition, if_rest->condition)) {
            // Two ifs with matching conditions
            Stmt then_case = mutate(Block::make(if_first->then_case, if_rest->then_case));
            Stmt else_case;
            if (if_first->else_case.defined() && if_rest->else_case.defined()) {
                else_case = mutate(Block::make(if_first->else_case, if_rest->else_case));
            } else if (if_first->else_case.defined()) {
                // We already simplified the body of the ifs.
                else_case = if_first->else_case;
            } else {
                else_case = if_rest->else_case;
            }
            stmt = IfThenElse::make(if_first->condition, then_case, else_case);
        } else if (if_first &&
                   if_rest &&
                   !if_rest->else_case.defined() &&
                   is_one(mutate((if_first->condition && if_rest->condition) == if_rest->condition))) {
            // Two ifs where the second condition is tighter than
            // the first condition.  The second if can be nested
            // inside the first one, because if it's true the
            // first one must also be true.
            Stmt then_case = mutate(Block::make(if_first->then_case, if_rest));
            Stmt else_case = mutate(if_first->else_case);
            stmt = IfThenElse::make(if_first->condition, then_case, else_case);
        } else if (op->first.same_as(first) &&
                   op->rest.same_as(rest)) {
            stmt = op;
        } else {
            stmt = Block::make(first, rest);
        }
    }
};

Expr simplify(Expr e, bool simplify_lets,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(simplify_lets, &bounds, &alignment).mutate(e);
}

Stmt simplify(Stmt s, bool simplify_lets,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(simplify_lets, &bounds, &alignment).mutate(s);
}

class SimplifyExprs : public IRMutator {
public:
    using IRMutator::mutate;
    Expr mutate(Expr e) {
        return simplify(e);
    }
};

Stmt simplify_exprs(Stmt s) {
    return SimplifyExprs().mutate(s);
}

bool can_prove(Expr e) {
    internal_assert(e.type().is_bool())
        << "Argument to can_prove is not a boolean Expr: " << e << "\n";
    return is_one(simplify(e));
}

namespace {

void check(Expr a, Expr b) {
    //debug(0) << "Checking that " << a << " -> " << b << "\n";
    Expr simpler = simplify(a);
    if (!equal(simpler, b)) {
        internal_error
            << "\nSimplification failure:\n"
            << "Input: " << a << '\n'
            << "Output: " << simpler << '\n'
            << "Expected output: " << b << '\n';
    }
}

void check(Stmt a, Stmt b) {
    //debug(0) << "Checking that " << a << " -> " << b << "\n";
    Stmt simpler = simplify(a);
    if (!equal(simpler, b)) {
        internal_error
            << "\nSimplification failure:\n"
            << "Input: " << a << '\n'
            << "Output: " << simpler << '\n'
            << "Expected output: " << b << '\n';
    }
}

void check_in_bounds(Expr a, Expr b, const Scope<Interval> &bi) {
    //debug(0) << "Checking that " << a << " -> " << b << "\n";
    Expr simpler = simplify(a, true, bi);
    if (!equal(simpler, b)) {
        internal_error
            << "\nSimplification failure:\n"
            << "Input: " << a << '\n'
            << "Output: " << simpler << '\n'
            << "Expected output: " << b << '\n';
    }
}

// Helper functions to use in the tests below
Expr interleave_vectors(vector<Expr> e) {
    Type t = e[0].type().with_lanes(e[0].type().lanes() * e.size());
    return Call::make(t, Call::interleave_vectors, e, Call::PureIntrinsic);
}

Expr ramp(Expr base, Expr stride, int w) {
    return Ramp::make(base, stride, w);
}

Expr broadcast(Expr base, int w) {
    return Broadcast::make(base, w);
}

void check_casts() {
    Expr x = Var("x");

    check(cast(Int(32), cast(Int(32), x)), x);
    check(cast(Float(32), 3), 3.0f);
    check(cast(Int(32), 5.0f), 5);

    check(cast(Int(32), cast(Int(8), 3)), 3);
    check(cast(Int(32), cast(Int(8), 1232)), -48);

    // Check evaluation of constant expressions involving casts
    check(cast(UInt(16), 53) + cast(UInt(16), 87), make_const(UInt(16), 140));
    check(cast(Int(8), 127) + cast(Int(8), 1), make_const(Int(8), -128));
    check(cast(UInt(16), -1) - cast(UInt(16), 1), make_const(UInt(16), 65534));
    check(cast(Int(16), 4) * cast(Int(16), -5), make_const(Int(16), -20));
    check(cast(Int(16), 16) / cast(Int(16), 4), make_const(Int(16), 4));
    check(cast(Int(16), 23) % cast(Int(16), 5), make_const(Int(16), 3));
    check(min(cast(Int(16), 30000), cast(Int(16), -123)), make_const(Int(16), -123));
    check(max(cast(Int(16), 30000), cast(Int(16), 65000)), make_const(Int(16), 30000));
    check(cast(UInt(16), -1) == cast(UInt(16), 65535), const_true());
    check(cast(UInt(16), 65) == cast(UInt(16), 66), const_false());
    check(cast(UInt(16), -1) < cast(UInt(16), 65535), const_false());
    check(cast(UInt(16), 65) < cast(UInt(16), 66), const_true());
    check(cast(UInt(16), 123.4f), make_const(UInt(16), 123));
    check(cast(Float(32), cast(UInt(16), 123456.0f)), 57920.0f);
    // Specific checks for 32 bit unsigned expressions - ensure simplifications are actually unsigned.
    // 4000000000 (4 billion) is less than 2^32 but more than 2^31.  As an int, it is negative.
    check(cast(UInt(32), (int) 4000000000UL) + cast(UInt(32), 5), make_const(UInt(32), (int) 4000000005UL));
    check(cast(UInt(32), (int) 4000000000UL) - cast(UInt(32), 5), make_const(UInt(32), (int) 3999999995UL));
    check(cast(UInt(32), (int) 4000000000UL) / cast(UInt(32), 5), make_const(UInt(32), 800000000));
    check(cast(UInt(32), 800000000) * cast(UInt(32), 5), make_const(UInt(32), (int) 4000000000UL));
    check(cast(UInt(32), (int) 4000000023UL) % cast(UInt(32), 100), make_const(UInt(32), 23));
    check(min(cast(UInt(32), (int) 4000000023UL) , cast(UInt(32), 1000)), make_const(UInt(32), (int) 1000));
    check(max(cast(UInt(32), (int) 4000000023UL) , cast(UInt(32), 1000)), make_const(UInt(32), (int) 4000000023UL));
    check(cast(UInt(32), (int) 4000000023UL) < cast(UInt(32), 1000), const_false());
    check(cast(UInt(32), (int) 4000000023UL) == cast(UInt(32), 1000), const_false());

    check(cast(Float(64), 0.5f), Expr(0.5));
    check((x - cast(Float(64), 0.5f)) * (x - cast(Float(64), 0.5f)),
          (x + Expr(-0.5)) * (x + Expr(-0.5)));

    check(cast(Int(64, 3), ramp(5.5f, 2.0f, 3)),
          cast(Int(64, 3), ramp(5.5f, 2.0f, 3)));
    check(cast(Int(64, 3), ramp(x, 2, 3)),
          ramp(cast(Int(64), x), cast(Int(64), 2), 3));

    // Check cancellations can occur through casts
    check(cast(Int(64), x + 1) - cast(Int(64), x), cast(Int(64), 1));
    check(cast(Int(64), 1 + x) - cast(Int(64), x), cast(Int(64), 1));
    // But only when overflow is undefined for the type
    check(cast(UInt(8), x + 1) - cast(UInt(8), x),
          cast(UInt(8), x + 1) - cast(UInt(8), x));
}

void check_algebra() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w"), v = Var("v");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);
    Expr t = const_true(), f = const_false();

    check(3 + x, x + 3);
    check(x + 0, x);
    check(0 + x, x);
    check(Expr(ramp(x, 2, 3)) + Expr(ramp(y, 4, 3)), ramp(x+y, 6, 3));
    check(Expr(broadcast(4.0f, 5)) + Expr(ramp(3.25f, 4.5f, 5)), ramp(7.25f, 4.5f, 5));
    check(Expr(ramp(3.25f, 4.5f, 5)) + Expr(broadcast(4.0f, 5)), ramp(7.25f, 4.5f, 5));
    check(Expr(broadcast(3, 3)) + Expr(broadcast(1, 3)), broadcast(4, 3));
    check((x + 3) + 4, x + 7);
    check(4 + (3 + x), x + 7);
    check((x + 3) + y, (x + y) + 3);
    check(y + (x + 3), (y + x) + 3);
    check((3 - x) + x, 3);
    check(x + (3 - x), 3);
    check(x*y + x*z, x*(y+z));
    check(x*y + z*x, x*(y+z));
    check(y*x + x*z, x*(y+z));
    check(y*x + z*x, x*(y+z));

    check(x - 0, x);
    check((x/y) - (x/y), 0);
    check(x - 2, x + (-2));
    check(Expr(ramp(x, 2, 3)) - Expr(ramp(y, 4, 3)), ramp(x-y, -2, 3));
    check(Expr(broadcast(4.0f, 5)) - Expr(ramp(3.25f, 4.5f, 5)), ramp(0.75f, -4.5f, 5));
    check(Expr(ramp(3.25f, 4.5f, 5)) - Expr(broadcast(4.0f, 5)), ramp(-0.75f, 4.5f, 5));
    check(Expr(broadcast(3, 3)) - Expr(broadcast(1, 3)), broadcast(2, 3));
    check((x + y) - x, y);
    check((x + y) - y, x);
    check(x - (x + y), 0 - y);
    check(x - (y + x), 0 - y);
    check((x + 3) - 2, x + 1);
    check((x + 3) - y, (x - y) + 3);
    check((x - 3) - y, (x - y) + (-3));
    check(x - (y - 2), (x - y) + 2);
    check(3 - (y - 2), 5 - y);
    check(x - (0 - y), x + y);
    check(x + (0 - y), x - y);
    check((0 - x) + y, y - x);
    check(x*y - x*z, x*(y-z));
    check(x*y - z*x, x*(y-z));
    check(y*x - x*z, x*(y-z));
    check(y*x - z*x, x*(y-z));
    check(x - y*-2, x + y*2);
    check(x + y*-2, x - y*2);
    check(x*-2 + y, y - x*2);
    check(xf - yf*-2.0f, xf + y*2.0f);
    check(xf + yf*-2.0f, xf - y*2.0f);
    check(xf*-2.0f + yf, yf - x*2.0f);

    check(x - (x/8)*8, x % 8);
    check((x/8)*8 - x, -(x % 8));
    check((x/8)*8 < x + y, 0 < x%8 + y);
    check((x/8)*8 < x - y, y < x%8);
    check((x/8)*8 < x, 0 < x%8);
    check(((x+3)/8)*8 < x + y, 3 < (x+3)%8 + y);
    check(((x+3)/8)*8 < x - y, y < (x+3)%8 + (-3));
    check(((x+3)/8)*8 < x, 3 < (x+3)%8);

    check(x*0, 0);
    check(0*x, 0);
    check(x*1, x);
    check(1*x, x);
    check(Expr(2.0f)*4.0f, 8.0f);
    check(Expr(2)*4, 8);
    check((3*x)*4, x*12);
    check(4*(3+x), x*4 + 12);
    check(Expr(broadcast(4.0f, 5)) * Expr(ramp(3.0f, 4.0f, 5)), ramp(12.0f, 16.0f, 5));
    check(Expr(ramp(3.0f, 4.0f, 5)) * Expr(broadcast(2.0f, 5)), ramp(6.0f, 8.0f, 5));
    check(Expr(broadcast(3, 3)) * Expr(broadcast(2, 3)), broadcast(6, 3));

    check(x*y + x, x*(y + 1));
    check(x*y - x, x*(y + -1));
    check(x + x*y, x*(y + 1));
    check(x - x*y, x*(1 - y));
    check(x*y + y, (x + 1)*y);
    check(x*y - y, (x + -1)*y);
    check(y + x*y, (x + 1)*y);
    check(y - x*y, (1 - x)*y);

    check(0/x, 0);
    check(x/1, x);
    check(x/x, 1);
    check(Expr(7)/3, 2);
    check(Expr(6.0f)/2.0f, 3.0f);
    check((x / 3) / 4, x / 12);
    check((x*4)/2, x*2);
    check((x*2)/4, x/2);
    check((x*4 + y)/2, x*2 + y/2);
    check((y + x*4)/2, y/2 + x*2);
    check((x*4 - y)/2, x*2 - y/2);
    check((y - x*4)/2, y/2 - x*2);
    check((x + 3)/2 + 7, (x + 17)/2);
    check((x/2 + 3)/5, (x + 6)/10);
    check((x + 8)/2, x/2 + 4);
    check((x - y)*-2, (y - x)*2);
    check((xf - yf)*-2.0f, (yf - xf)*2.0f);

    check(xf / 4.0f, xf * 0.25f);

    // Some quaternary rules with cancellations
    check((x + y) - (z + y), x - z);
    check((x + y) - (y + z), x - z);
    check((y + x) - (z + y), x - z);
    check((y + x) - (y + z), x - z);

    check((x - y) - (z - y), x - z);
    check((y - z) - (y - x), x - z);

    check((x*8) % 4, 0);
    check((x*8 + y) % 4, y % 4);
    check((y + 8) % 4, y % 4);
    check((y + x*8) % 4, y % 4);
    check((y*16 + 13) % 2, 1);

    // Check an optimization important for fusing dimensions
    check((x/3)*3 + x%3, x);
    check(x%3 + (x/3)*3, x);

    check(((x/3)*3 + y) + x%3, x + y);
    check((x%3 + y) + (x/3)*3, x + y);

    check((y + x%3) + (x/3)*3, y + x);
    check((y + (x/3*3)) + x%3, y + x);

    // Almost-cancellations through integer divisions. These rules all
    // deduplicate x and wrap it in a modulo operator, neutering it
    // for the purposes of bounds inference. Patterns below look
    // confusing, but were brute-force tested.
    check((x + 17)/3 - (x + 7)/3, ((x+1)%3 + 10)/3);
    check((x + 17)/3 - (x + y)/3, (19 - y - (x+2)%3)/3);
    check((x + y )/3 - (x + 7)/3, ((x+1)%3 + y + -7)/3);
    check( x      /3 - (x + y)/3, (2 - y - x % 3)/3);
    check((x + y )/3 -  x     /3, (x%3 + y)/3);
    check( x      /3 - (x + 7)/3, (-5 - x%3)/3);
    check((x + 17)/3 -  x     /3, (x%3 + 17)/3);
    check((x + 17)/3 - (x - y)/3, (y - (x+2)%3 + 19)/3);
    check((x - y )/3 - (x + 7)/3, ((x+1)%3 - y + (-7))/3);
    check( x      /3 - (x - y)/3, (y - x%3 + 2)/3);
    check((x - y )/3 -  x     /3, (x%3 - y)/3);

    // Check some specific expressions involving div and mod
    check(Expr(23) / 4, Expr(5));
    check(Expr(-23) / 4, Expr(-6));
    check(Expr(-23) / -4, Expr(6));
    check(Expr(23) / -4, Expr(-5));
    check(Expr(-2000000000) / 1000000001, Expr(-2));
    check(Expr(23) % 4, Expr(3));
    check(Expr(-23) % 4, Expr(1));
    check(Expr(-23) % -4, Expr(1));
    check(Expr(23) % -4, Expr(3));
    check(Expr(-2000000000) % 1000000001, Expr(2));

    check(Expr(3) + Expr(8), 11);
    check(Expr(3.25f) + Expr(7.75f), 11.0f);

    check(Expr(7) % 2, 1);
    check(Expr(7.25f) % 2.0f, 1.25f);
    check(Expr(-7.25f) % 2.0f, 0.75f);
    check(Expr(-7.25f) % -2.0f, -1.25f);
    check(Expr(7.25f) % -2.0f, -0.75f);
}

void check_vectors() {
    Expr x = Var("x"), y = Var("y"), z = Var("z");

    check(Expr(broadcast(y, 4)) / Expr(broadcast(x, 4)),
          Expr(broadcast(y/x, 4)));
    check(Expr(ramp(x, 4, 4)) / 2, ramp(x/2, 2, 4));
    check(Expr(ramp(x, -4, 7)) / 2, ramp(x/2, -2, 7));
    check(Expr(ramp(x, 4, 5)) / -2, ramp(x/-2, -2, 5));
    check(Expr(ramp(x, -8, 5)) / -2, ramp(x/-2, 4, 5));

    check(Expr(ramp(4*x, 1, 4)) / 4, broadcast(x, 4));
    check(Expr(ramp(x*4, 1, 3)) / 4, broadcast(x, 3));
    check(Expr(ramp(x*8, 2, 4)) / 8, broadcast(x, 4));
    check(Expr(ramp(x*8, 3, 3)) / 8, broadcast(x, 3));
    check(Expr(ramp(0, 1, 8)) % 16, Expr(ramp(0, 1, 8)));
    check(Expr(ramp(8, 1, 8)) % 16, Expr(ramp(8, 1, 8)));
    check(Expr(ramp(9, 1, 8)) % 16, Expr(ramp(9, 1, 8)) % 16);
    check(Expr(ramp(16, 1, 8)) % 16, Expr(ramp(0, 1, 8)));
    check(Expr(ramp(0, 1, 8)) % 8, Expr(ramp(0, 1, 8)));
    check(Expr(ramp(x*8+17, 1, 4)) % 8, Expr(ramp(1, 1, 4)));
    check(Expr(ramp(x*8+17, 1, 8)) % 8, Expr(ramp(1, 1, 8) % 8));


    check(Expr(broadcast(x, 4)) % Expr(broadcast(y, 4)),
          Expr(broadcast(x % y, 4)));
    check(Expr(ramp(x, 2, 4)) % (broadcast(2, 4)),
          broadcast(x % 2, 4));
    check(Expr(ramp(2*x+1, 4, 4)) % (broadcast(2, 4)),
          broadcast(1, 4));

    check(ramp(0, 1, 4) == broadcast(2, 4),
          ramp(-2, 1, 4) == broadcast(0, 4));
}

void check_bounds() {
    Expr x = Var("x"), y = Var("y"), z = Var("z");

    check(min(Expr(7), 3), 3);
    check(min(Expr(4.25f), 1.25f), 1.25f);
    check(min(broadcast(x, 4), broadcast(y, 4)),
          broadcast(min(x, y), 4));
    check(min(x, x+3), x);
    check(min(x+4, x), x);
    check(min(x-1, x+2), x+(-1));
    check(min(7, min(x, 3)), min(x, 3));
    check(min(min(x, y), x), min(x, y));
    check(min(min(x, y), y), min(x, y));
    check(min(x, min(x, y)), min(x, y));
    check(min(y, min(x, y)), min(x, y));

    check(max(Expr(7), 3), 7);
    check(max(Expr(4.25f), 1.25f), 4.25f);
    check(max(broadcast(x, 4), broadcast(y, 4)),
          broadcast(max(x, y), 4));
    check(max(x, x+3), x+3);
    check(max(x+4, x), x+4);
    check(max(x-1, x+2), x+2);
    check(max(7, max(x, 3)), max(x, 7));
    check(max(max(x, y), x), max(x, y));
    check(max(max(x, y), y), max(x, y));
    check(max(x, max(x, y)), max(x, y));
    check(max(y, max(x, y)), max(x, y));

    // Check that simplifier can recognise instances where the extremes of the
    // datatype appear as constants in comparisons, Min and Max expressions.
    // The result of min/max with extreme is known to be either the extreme or
    // the other expression.  The result of < or > comparison is known to be true or false.
    check(x <= Int(32).max(), const_true());
    check(cast(Int(16), x) >= Int(16).min(), const_true());
    check(x < Int(32).min(), const_false());
    check(min(cast(UInt(16), x), cast(UInt(16), 65535)), cast(UInt(16), x));
    check(min(x, Int(32).max()), x);
    check(min(Int(32).min(), x), Int(32).min());
    check(max(cast(Int(8), x), cast(Int(8), -128)), cast(Int(8), x));
    check(max(x, Int(32).min()), x);
    check(max(x, Int(32).max()), Int(32).max());
    // Check that non-extremes do not lead to incorrect simplification
    check(max(cast(Int(8), x), cast(Int(8), -127)), max(cast(Int(8), x), make_const(Int(8), -127)));

    // Some quaternary rules with cancellations
    check((x + y) - (z + y), x - z);
    check((x + y) - (y + z), x - z);
    check((y + x) - (z + y), x - z);
    check((y + x) - (y + z), x - z);

    check((x - y) - (z - y), x - z);
    check((y - z) - (y - x), x - z);

    check((x + 3) / 4 - (x + 2) / 4, ((x + 2) % 4 + 1)/4);

    check(x - min(x + y, z), max(-y, x-z));
    check(x - min(y + x, z), max(-y, x-z));
    check(x - min(z, x + y), max(-y, x-z));
    check(x - min(z, y + x), max(-y, x-z));

    check(min(x + y, z) - x, min(y, z-x));
    check(min(y + x, z) - x, min(y, z-x));
    check(min(z, x + y) - x, min(y, z-x));
    check(min(z, y + x) - x, min(y, z-x));

    check(min(x + y, z + y), min(x, z) + y);
    check(min(y + x, z + y), min(x, z) + y);
    check(min(x + y, y + z), min(x, z) + y);
    check(min(y + x, y + z), min(x, z) + y);

    check(min(x, y) - min(y, x), 0);
    check(max(x, y) - max(y, x), 0);

    check(min(123 - x, 1 - x), 1 - x);
    check(max(123 - x, 1 - x), 123 - x);

    check(min(x*43, y*43), min(x, y)*43);
    check(max(x*43, y*43), max(x, y)*43);
    check(min(x*-43, y*-43), max(x, y)*-43);
    check(max(x*-43, y*-43), min(x, y)*-43);

    check(min(min(x, 4), y), min(min(x, y), 4));
    check(max(max(x, 4), y), max(max(x, y), 4));

    check(min(x*8, 24), min(x, 3)*8);
    check(max(x*8, 24), max(x, 3)*8);
    check(min(x*-8, 24), max(x, -3)*-8);
    check(max(x*-8, 24), min(x, -3)*-8);

    check(min(clamp(x, -10, 14), clamp(y, -10, 14)), clamp(min(x, y), -10, 14));

    check(min(x/4, y/4), min(x, y)/4);
    check(max(x/4, y/4), max(x, y)/4);

    check(min(x/(-4), y/(-4)), max(x, y)/(-4));
    check(max(x/(-4), y/(-4)), min(x, y)/(-4));

    //check(max(x, 16) - 16, max(x + -16, 0));
    //check(min(x, -4) + 7, min(x + 7, 3));

    // Min and max of clamped expressions
    check(min(clamp(x+1, y, z), clamp(x-1, y, z)), clamp(x+(-1), y, z));
    check(max(clamp(x+1, y, z), clamp(x-1, y, z)), clamp(x+1, y, z));

    // Additions that cancel a term inside a min or max
    check(x + min(y - x, z), min(y, z + x));
    check(x + max(y - x, z), max(y, z + x));
    check(min(y + (-2), z) + 2, min(y, z + 2));
    check(max(y + (-2), z) + 2, max(y, z + 2));

    check(x + min(y - x, z), min(y, z + x));
    check(x + max(y - x, z), max(y, z + x));
    check(min(y + (-2), z) + 2, min(y, z + 2));
    check(max(y + (-2), z) + 2, max(y, z + 2));

    // Min/Max distributive law
    check(max(max(x, y), max(x, z)), max(max(y, z), x));
    check(min(max(x, y), max(x, z)), max(min(y, z), x));
    check(min(min(x, y), min(x, z)), min(min(y, z), x));
    check(max(min(x, y), min(x, z)), min(max(y, z), x));

    // Mins of expressions and rounded up versions of them
    check(min(((x+7)/8)*8, x), x);
    check(min(x, ((x+7)/8)*8), x);

    check(min(((x+7)/8)*8, max(x, 8)), max(x, 8));
    check(min(max(x, 8), ((x+7)/8)*8), max(x, 8));

    // Pull constants all the way outside of a clamp
    //check(clamp(x + 1, -10, 15), clamp(x, -11, 14) + 1);
    //check(clamp(x + 1, y - 10, 15), clamp(x, y + (-11), 14) + 1);

    check(min(x, likely(x)), likely(x));
    check(min(likely(x), x), likely(x));
    check(max(x, likely(x)), likely(x));
    check(max(likely(x), x), likely(x));
    check(select(x > y, likely(x), x), likely(x));
    check(select(x > y, x, likely(x)), likely(x));

    check(min(x + 1, y) - min(x, y - 1), 1);
    check(max(x + 1, y) - max(x, y - 1), 1);
    check(min(x + 1, y) - min(y - 1, x), 1);
    check(max(x + 1, y) - max(y - 1, x), 1);

    // min and max on constant ramp v broadcast
    check(max(ramp(0, 1, 8), 0), ramp(0, 1, 8));
    check(min(ramp(0, 1, 8), 7), ramp(0, 1, 8));
    check(max(ramp(0, 1, 8), 7), broadcast(7, 8));
    check(min(ramp(0, 1, 8), 0), broadcast(0, 8));
    check(min(ramp(0, 1, 8), 4), min(ramp(0, 1, 8), 4));

    check(max(ramp(7, -1, 8), 0), ramp(7, -1, 8));
    check(min(ramp(7, -1, 8), 7), ramp(7, -1, 8));
    check(max(ramp(7, -1, 8), 7), broadcast(7, 8));
    check(min(ramp(7, -1, 8), 0), broadcast(0, 8));
    check(min(ramp(7, -1, 8), 4), min(ramp(7, -1, 8), 4));

    check(max(0, ramp(0, 1, 8)), ramp(0, 1, 8));
    check(min(7, ramp(0, 1, 8)), ramp(0, 1, 8));

    check(min(8 - x, 2), 8 - max(x, 6));
    check(max(3, 77 - x), 77 - min(x, 74));
    check(min(max(8-x, 0), 8), 8 - max(min(x, 8), 0));

    check(x - min(x, 2), max(x + -2, 0));
    check(x - max(x, 2), min(x + -2, 0));
    check(min(x, 2) - x, 2 - max(x, 2));
    check(max(x, 2) - x, 2 - min(x, 2));
    check(x - min(2, x), max(x + -2, 0));
    check(x - max(2, x), min(x + -2, 0));
    check(min(2, x) - x, 2 - max(x, 2));
    check(max(2, x) - x, 2 - min(x, 2));

}

void check_boolean() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);
    Expr t = const_true(), f = const_false();
    Expr b1 = Variable::make(Bool(), "b1");
    Expr b2 = Variable::make(Bool(), "b2");

    check(x == x, t);
    check(x == (x+1), f);
    check(x-2 == y+3, (x-y) == 5);
    check(x+y == y+z, x == z);
    check(y+x == y+z, x == z);
    check(x+y == z+y, x == z);
    check(y+x == z+y, x == z);
    check((y+x)*17 == (z+y)*17, x == z);
    check(x*0 == y*0, t);
    check(x == x+y, y == 0);
    check(x+y == x, y == 0);
    check(100 - x == 99 - y, (y-x) == -1);

    check(x < x, f);
    check(x < (x+1), t);
    check(x-2 < y+3, x < y+5);
    check(x+y < y+z, x < z);
    check(y+x < y+z, x < z);
    check(x+y < z+y, x < z);
    check(y+x < z+y, x < z);
    check((y+x)*17 < (z+y)*17, x < z);
    check(x*0 < y*0, f);
    check(x < x+y, 0 < y);
    check(x+y < x, y < 0);

    check(select(x < 3, 2, 2), 2);
    check(select(x < (x+1), 9, 2), 9);
    check(select(x > (x+1), 9, 2), 2);
    // Selects of comparisons should always become selects of LT or selects of EQ
    check(select(x != 5, 2, 3), select(x == 5, 3, 2));
    check(select(x >= 5, 2, 3), select(x < 5, 3, 2));
    check(select(x <= 5, 2, 3), select(5 < x, 3, 2));
    check(select(x > 5, 2, 3), select(5 < x, 2, 3));

    check(select(x > 5, 2, 3) + select(x > 5, 6, 2), select(5 < x, 8, 5));
    check(select(x > 5, 8, 3) - select(x > 5, 6, 2), select(5 < x, 2, 1));

    check((1 - xf)*6 < 3, 0.5f < xf);

    check(!f, t);
    check(!t, f);
    check(!(x < y), y <= x);
    check(!(x > y), x <= y);
    check(!(x >= y), x < y);
    check(!(x <= y), y < x);
    check(!(x == y), x != y);
    check(!(x != y), x == y);
    check(!(!(x == 0)), x == 0);
    check(!Expr(broadcast(x > y, 4)),
          broadcast(x <= y, 4));

    check(b1 || !b1, t);
    check(!b1 || b1, t);
    check(b1 && !b1, f);
    check(!b1 && b1, f);
    check(b1 && b1, b1);
    check(b1 || b1, b1);
    check(broadcast(b1, 4) || broadcast(!b1, 4), broadcast(t, 4));
    check(broadcast(!b1, 4) || broadcast(b1, 4), broadcast(t, 4));
    check(broadcast(b1, 4) && broadcast(!b1, 4), broadcast(f, 4));
    check(broadcast(!b1, 4) && broadcast(b1, 4), broadcast(f, 4));
    check(broadcast(b1, 4) && broadcast(b1, 4), broadcast(b1, 4));
    check(broadcast(b1, 4) || broadcast(b1, 4), broadcast(b1, 4));

    check(t && (x < 0), x < 0);
    check(f && (x < 0), f);
    check(t || (x < 0), t);
    check(f || (x < 0), x < 0);

    check(x == y || y != x, t);
    check(x == y || x != y, t);
    check(x == y && x != y, f);
    check(x == y && y != x, f);
    check(x < y || x >= y, t);
    check(x <= y || x > y, t);
    check(x < y && x >= y, f);
    check(x <= y && x > y, f);

    check(x <= max(x, y), t);
    check(x <  min(x, y), f);
    check(min(x, y) <= x, t);
    check(max(x, y) <  x, f);
    check(max(x, y) <= y, x <= y);
    check(min(x, y) >= y, y <= x);

    check((1 < y) && (2 < y), 2 < y);

    check(x*5 < 4, x < 1);
    check(x*5 < 5, x < 1);
    check(x*5 < 6, x < 2);
    check(x*5 <= 4, x <= 0);
    check(x*5 <= 5, x <= 1);
    check(x*5 <= 6, x <= 1);
    check(x*5 > 4, 0 < x);
    check(x*5 > 5, 1 < x);
    check(x*5 > 6, 1 < x);
    check(x*5 >= 4, 1 <= x);
    check(x*5 >= 5, 1 <= x);
    check(x*5 >= 6, 2 <= x);

    check(x/4 < 3, x < 12);
    check(3 < x/4, 15 < x);

    check(4 - x <= 0, 4 <= x);

    check((x/8)*8 < x - 8, f);
    check((x/8)*8 < x - 9, f);
    check((x/8)*8 < x - 7, f);
    check((x/8)*8 < x - 6, 6 < x % 8);
    check(ramp(x*4, 1, 4) < broadcast(y*4, 4), broadcast(x < y, 4));
    check(ramp(x*8, 1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 1, 1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 4, 1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 8, 1, 4) < broadcast(y*8, 4), broadcast(x < y + (-1), 4));
    check(ramp(x*8 + 5, 1, 4) < broadcast(y*8, 4), ramp(x*8 + 5, 1, 4) < broadcast(y*8, 4));
    check(ramp(x*8 - 1, 1, 4) < broadcast(y*8, 4), ramp(x*8 + (-1), 1, 4) < broadcast(y*8, 4));
    check(ramp(x*8, 1, 4) < broadcast(y*4, 4), broadcast(x*2 < y, 4));
    check(ramp(x*8, 2, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 1, 2, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 2, 2, 4) < broadcast(y*8, 4), ramp(x*8 + 2, 2, 4) < broadcast(y*8, 4));
    check(ramp(x*8, 3, 4) < broadcast(y*8, 4), ramp(x*8, 3, 4) < broadcast(y*8, 4));
    check(select(ramp((x/16)*16, 1, 8) < broadcast((y/8)*8, 8), broadcast(1, 8), broadcast(3, 8)),
          select((x/16)*2 < y/8, broadcast(1, 8), broadcast(3, 8)));

    check(ramp(x*8, -1, 4) < broadcast(y*8, 4), ramp(x*8, -1, 4) < broadcast(y*8, 4));
    check(ramp(x*8 + 1, -1, 4) < broadcast(y*8, 4), ramp(x*8 + 1, -1, 4) < broadcast(y*8, 4));
    check(ramp(x*8 + 4, -1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 8, -1, 4) < broadcast(y*8, 4), ramp(x*8 + 8, -1, 4) < broadcast(y*8, 4));
    check(ramp(x*8 + 5, -1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 - 1, -1, 4) < broadcast(y*8, 4), broadcast(x < y + 1, 4));

    // Check anded conditions apply to the then case only
    check(IfThenElse::make(x == 4 && y == 5,
                           Evaluate::make(z + x + y),
                           Evaluate::make(z + x - y)),
          IfThenElse::make(x == 4 && y == 5,
                           Evaluate::make(z + 9),
                           Evaluate::make(z + x - y)));

    // Check ored conditions apply to the else case only
    check(IfThenElse::make(b1 || b2,
                           Evaluate::make(select(b1, x+3, y+4) + select(b2, x+5, y+7)),
                           Evaluate::make(select(b1, x+3, y+8) - select(b2, x+5, y+7))),
          IfThenElse::make(b1 || b2,
                           Evaluate::make(select(b1, x+3, y+4) + select(b2, x+5, y+7)),
                           Evaluate::make(1)));

    // Check single conditions apply to both cases of an ifthenelse
    check(IfThenElse::make(b1,
                           Evaluate::make(select(b1, x, y)),
                           Evaluate::make(select(b1, z, w))),
          IfThenElse::make(b1,
                           Evaluate::make(x),
                           Evaluate::make(w)));

    check(IfThenElse::make(x < y,
                           IfThenElse::make(x < y, Evaluate::make(y), Evaluate::make(x)),
                           Evaluate::make(x)),
          IfThenElse::make(x < y,
                           Evaluate::make(y),
                           Evaluate::make(x)));

    check(Block::make(IfThenElse::make(x < y, Evaluate::make(x+1), Evaluate::make(x+2)),
                      IfThenElse::make(x < y, Evaluate::make(x+3), Evaluate::make(x+4))),
          IfThenElse::make(x < y,
                           Block::make(Evaluate::make(x+1), Evaluate::make(x+3)),
                           Block::make(Evaluate::make(x+2), Evaluate::make(x+4))));

    check(Block::make(IfThenElse::make(x < y, Evaluate::make(x+1)),
                      IfThenElse::make(x < y, Evaluate::make(x+2))),
          IfThenElse::make(x < y, Block::make(Evaluate::make(x+1), Evaluate::make(x+2))));

    check(Block::make(IfThenElse::make(x < y, Evaluate::make(x+1), Evaluate::make(x+2)),
                      IfThenElse::make(x < y, Evaluate::make(x+3))),
          IfThenElse::make(x < y,
                           Block::make(Evaluate::make(x+1), Evaluate::make(x+3)),
                           Evaluate::make(x+2)));

    check(Block::make(IfThenElse::make(x < y, Evaluate::make(x+1)),
                      IfThenElse::make(x < y, Evaluate::make(x+2), Evaluate::make(x+3))),
          IfThenElse::make(x < y,
                           Block::make(Evaluate::make(x+1), Evaluate::make(x+2)),
                           Evaluate::make(x+3)));

    // Check conditions involving entire exprs
    Expr foo = x + 3*y;
    Expr foo_simple = x + y*3;
    check(IfThenElse::make(foo == 17,
                           Evaluate::make(x+foo+1),
                           Evaluate::make(x+foo+2)),
          IfThenElse::make(foo_simple == 17,
                           Evaluate::make(x+18),
                           Evaluate::make(x+foo_simple+2)));

    check(IfThenElse::make(foo != 17,
                           Evaluate::make(x+foo+1),
                           Evaluate::make(x+foo+2)),
          IfThenElse::make(foo_simple != 17,
                           Evaluate::make(x+foo_simple+1),
                           Evaluate::make(x+19)));

    // Simplifications of selects
    check(select(x == 3, 5, 7) + 7, select(x == 3, 12, 14));
    check(select(x == 3, 5, 7) - 7, select(x == 3, -2, 0));
    check(select(x == 3, 5, y) - y, select(x == 3, 5 - y, 0));
    check(select(x == 3, y, 5) - y, select(x == 3, 0, 5 - y));
    check(y - select(x == 3, 5, y), select(x == 3, y + (-5), 0));
    check(y - select(x == 3, y, 5), select(x == 3, 0, y + (-5)));

    check(select(x == 3, 5, 7) == 7, x != 3);
    check(select(x == 3, z, y) == z, (x == 3) || (y == z));

    check(select(x == 3, 4, 2) == 0, const_false());
    check(select(x == 3, y, 2) == 4, (x == 3) && (y == 4));
    check(select(x == 3, 2, y) == 4, (x != 3) && (y == 4));

    check(min(select(x == 2, y*3, 8), select(x == 2, y+8, y*7)),
          select(x == 2, min(y*3, y+8), min(y*7, 8)));

    check(max(select(x == 2, y*3, 8), select(x == 2, y+8, y*7)),
          select(x == 2, max(y*3, y+8), max(y*7, 8)));

    check(select(x == 2, x+1, x+5), x + select(x == 2, 1, 5));
    check(select(x == 2, x+y, x+z), x + select(x == 2, y, z));
    check(select(x == 2, y+x, x+z), x + select(x == 2, y, z));
    check(select(x == 2, y+x, z+x), select(x == 2, y, z) + x);
    check(select(x == 2, x+y, z+x), x + select(x == 2, y, z));
    check(select(x == 2, x*2, x*5), x * select(x == 2, 2, 5));
    check(select(x == 2, x*y, x*z), x * select(x == 2, y, z));
    check(select(x == 2, y*x, x*z), x * select(x == 2, y, z));
    check(select(x == 2, y*x, z*x), select(x == 2, y, z) * x);
    check(select(x == 2, x*y, z*x), x * select(x == 2, y, z));
    check(select(x == 2, x-y, x-z), x - select(x == 2, y, z));
    check(select(x == 2, y-x, z-x), select(x == 2, y, z) - x);
    check(select(x == 2, x+y, x-z), x + select(x == 2, y, 0-z));
    check(select(x == 2, y+x, x-z), x + select(x == 2, y, 0-z));
    check(select(x == 2, x-z, x+y), x + select(x == 2, 0-z, y));
    check(select(x == 2, x-z, y+x), x + select(x == 2, 0-z, y));


    {

        Expr b[12];
        for (int i = 0; i < 12; i++) {
            b[i] = Variable::make(Bool(), unique_name('b'));
        }

        // Some rules that collapse selects
        check(select(b[0], x, select(b[1], x, y)),
              select(b[0] || b[1], x, y));
        check(select(b[0], x, select(b[1], y, x)),
              select(b[0] || !b[1], x, y));
        check(select(b[0], select(b[1], x, y), x),
              select(b[0] && !b[1], y, x));
        check(select(b[0], select(b[1], y, x), x),
              select(b[0] && b[1], y, x));

        // Ternary boolean expressions in two variables
        check(b[0] || (b[0] && b[1]), b[0]);
        check((b[0] && b[1]) || b[0], b[0]);
        check(b[0] && (b[0] || b[1]), b[0]);
        check((b[0] || b[1]) && b[0], b[0]);
        check(b[0] && (b[0] && b[1]), b[0] && b[1]);
        check((b[0] && b[1]) && b[0], b[1] && b[0]);
        check(b[0] || (b[0] || b[1]), b[0] || b[1]);
        check((b[0] || b[1]) || b[0], b[1] || b[0]);

        // A nasty unsimplified boolean Expr seen in the wild
        Expr nasty = ((((((((((((((((((((((((((((((((((((((((((((b[0] && b[1]) || (b[2] && b[1])) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[6]) || (b[2] && b[6]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[3]) || (b[2] && b[3]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[7]) || (b[2] && b[7]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[4]) || (b[2] && b[4]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[8]) || (b[2] && b[8]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[5]) || (b[2] && b[5]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[10]) || (b[2] && b[10]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[9]) || (b[2] && b[9]))) || b[0]) || b[2]);
        check(nasty, b[0] || b[2]);
    }
}

void check_math() {
    Var x = Var("x");

    check(log(0.5f + 0.5f), 0.0f);
    check(exp(log(2.0f)), 2.0f);

    check(floor(0.98f), 0.0f);
    check(ceil(0.98f), 1.0f);
    check(round(0.6f), 1.0f);
    check(round(-0.5f), 0.0f);
    check(trunc(-1.6f), -1.0f);
    check(floor(round(x)), round(x));
    check(ceil(ceil(x)), ceil(x));
}

void check_overflow() {
    Expr overflowing[] = {
        make_const(Int(32), 0x7fffffff) + 1,
        make_const(Int(32), 0x7ffffff0) + 16,
        (make_const(Int(32), 0x7fffffff) +
         make_const(Int(32), 0x7fffffff)),
        make_const(Int(32), 0x08000000) * 16,
        (make_const(Int(32), 0x00ffffff) *
         make_const(Int(32), 0x00ffffff)),
        make_const(Int(32), 0x80000000) - 1,
        0 - make_const(Int(32), 0x80000000),
        make_const(Int(64), (int64_t)0x7fffffffffffffffLL) + 1,
        make_const(Int(64), (int64_t)0x7ffffffffffffff0LL) + 16,
        (make_const(Int(64), (int64_t)0x7fffffffffffffffLL) +
         make_const(Int(64), (int64_t)0x7fffffffffffffffLL)),
        make_const(Int(64), (int64_t)0x0800000000000000LL) * 16,
        (make_const(Int(64), (int64_t)0x00ffffffffffffffLL) *
         make_const(Int(64), (int64_t)0x00ffffffffffffffLL)),
        make_const(Int(64), (int64_t)0x8000000000000000LL) - 1,
        0 - make_const(Int(64), (int64_t)0x8000000000000000LL),
    };
    Expr not_overflowing[] = {
        make_const(Int(32), 0x7ffffffe) + 1,
        make_const(Int(32), 0x7fffffef) + 16,
        make_const(Int(32), 0x07ffffff) * 2,
        (make_const(Int(32), 0x0000ffff) *
         make_const(Int(32), 0x00008000)),
        make_const(Int(32), 0x80000001) - 1,
        0 - make_const(Int(32), 0x7fffffff),
        make_const(Int(64), (int64_t)0x7ffffffffffffffeLL) + 1,
        make_const(Int(64), (int64_t)0x7fffffffffffffefLL) + 16,
        make_const(Int(64), (int64_t)0x07ffffffffffffffLL) * 16,
        (make_const(Int(64), (int64_t)0x00000000ffffffffLL) *
         make_const(Int(64), (int64_t)0x0000000080000000LL)),
        make_const(Int(64), (int64_t)0x8000000000000001LL) - 1,
        0 - make_const(Int(64), (int64_t)0x7fffffffffffffffLL),
    };

    for (Expr e : overflowing) {
        internal_assert(!is_const(simplify(e)))
            << "Overflowing expression should not have simplified: " << e << "\n";
    }
    for (Expr e : not_overflowing) {
        internal_assert(is_const(simplify(e)))
            << "Non-everflowing expression should have simplified: " << e << "\n";
    }
}

}

void simplify_test() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w"), v = Var("v");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);
    Expr t = const_true(), f = const_false();

    check_casts();
    check_algebra();
    check_vectors();
    check_bounds();
    check_math();
    check_boolean();
    check_overflow();

    // Check bitshift operations
    check(cast(Int(16), x) << 10, cast(Int(16), x) * 1024);
    check(cast(Int(16), x) >> 10, cast(Int(16), x) / 1024);
    check(cast(Int(16), x) << -10, cast(Int(16), x) / 1024);
    // Correctly triggers a warning:
    //check(cast(Int(16), x) << 20, cast(Int(16), x) << 20);

    // Check that chains of widening casts don't lose the distinction
    // between zero-extending and sign-extending.
    check(cast(UInt(64), cast(UInt(32), cast(Int(8), -1))),
          UIntImm::make(UInt(64), 0xffffffffULL));

    v = Variable::make(Int(32, 4), "v");
    // Check constants get pushed inwards
    check(Let::make("x", 3, x+4), 7);

    // Check ramps in lets get pushed inwards
    check(Let::make("v", ramp(x*2+7, 3, 4), v + Expr(broadcast(2, 4))),
          ramp(x*2+9, 3, 4));

    // Check broadcasts in lets get pushed inwards
    check(Let::make("v", broadcast(x, 4), v + Expr(broadcast(2, 4))),
          broadcast(x+2, 4));

    // Check that dead lets get stripped
    check(Let::make("x", 3*y*y*y, 4), 4);
    check(Let::make("x", 0, 0), 0);

    // Test case with most negative 32-bit number, as constant to check that it is not negated.
    check(((x * (int32_t)0x80000000) + (y + z * (int32_t)0x80000000)),
          ((x * (int32_t)0x80000000) + (y + z * (int32_t)0x80000000)));

    // Check that constant args to a stringify get combined
    check(Call::make(type_of<const char *>(), Call::stringify, {3, string(" "), 4}, Call::Intrinsic),
          string("3 4"));

    check(Call::make(type_of<const char *>(), Call::stringify, {3, x, 4, string(", "), 3.4f}, Call::Intrinsic),
          Call::make(type_of<const char *>(), Call::stringify, {string("3"), x, string("4, 3.400000")}, Call::Intrinsic));

    // Check if we can simplify away comparison on vector types considering bounds.
    Scope<Interval> bounds_info;
    bounds_info.push("x", Interval(0,4));
    check_in_bounds(ramp(x, 1,4) < broadcast(0,4),  const_false(4), bounds_info);
    check_in_bounds(ramp(x, 1,4) < broadcast(8,4),  const_true(4),  bounds_info);
    check_in_bounds(ramp(x,-1,4) < broadcast(-4,4), const_false(4), bounds_info);
    check_in_bounds(ramp(x,-1,4) < broadcast(5,4),  const_true(4),  bounds_info);

    // Collapse some vector interleaves
    check(interleave_vectors({ramp(x, 2, 4), ramp(x+1, 2, 4)}), ramp(x, 1, 8));
    check(interleave_vectors({ramp(x, 4, 4), ramp(x+2, 4, 4)}), ramp(x, 2, 8));
    check(interleave_vectors({ramp(x-y, 2*y, 4), ramp(x, 2*y, 4)}), ramp(x-y, y, 8));
    check(interleave_vectors({ramp(x, 3, 4), ramp(x+1, 3, 4), ramp(x+2, 3, 4)}), ramp(x, 1, 12));

    // Now some ones that can't work
    {
        Expr e = interleave_vectors({ramp(x, 2, 4), ramp(x, 2, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 2, 4), ramp(x+2, 2, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 3, 4), ramp(x+1, 3, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 2, 4), ramp(y+1, 2, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 2, 4), ramp(x+1, 3, 4)});
        check(e, e);
    }

    // Now check that an interleave of some collapsible loads collapses into a single dense load
    {
        Expr load1 = Load::make(Float(32, 4), "buf", ramp(x, 2, 4), Buffer(), Parameter());
        Expr load2 = Load::make(Float(32, 4), "buf", ramp(x+1, 2, 4), Buffer(), Parameter());
        Expr load12 = Load::make(Float(32, 8), "buf", ramp(x, 1, 8), Buffer(), Parameter());
        check(interleave_vectors({load1, load2}), load12);

        // They don't collapse in the other order
        Expr e = interleave_vectors({load2, load1});
        check(e, e);

        // Or if the buffers are different
        Expr load3 = Load::make(Float(32, 4), "buf2", ramp(x+1, 2, 4), Buffer(), Parameter());
        e = interleave_vectors({load1, load3});
        check(e, e);

    }

    // This expression doesn't simplify, but it did cause exponential
    // slowdown at one stage.
    {
        Expr e = x;
        for (int i = 0; i < 100; i++) {
            e = max(e, 1)/2;
        }
        check(e, e);
    }

    std::cout << "Simplify test passed" << std::endl;
}
}
}
