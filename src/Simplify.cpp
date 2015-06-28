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

// Things that we can constant fold: Immediates and broadcasts of
// immediates.
bool is_simple_const(Expr e) {
    if (e.as<IntImm>()) return true;
    if (e.as<FloatImm>()) return true;
    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_simple_const(b->value);
    }
    return false;
}

// Is a constant representable as a certain type
int do_indirect_int_cast(Type t, int x) {
    if (t == UInt(1)) {
        return x&1;
    } else if (t.is_int() || t.is_uint()) {
        return int_cast_constant(t, x);
    } else if (t == Float(32)) {
        return (int)((float)x);
    } else if (t == Float(64)) {
        return (int)((double)x);
    } else {
        internal_error << "Can't do an indirect int cast via this type: " << t << "\n";
        return 0;
    }
}

// Returns true if T does not have a well defined overflow behavior.
bool no_overflow(Type T) {
    return T.is_float() || (T.is_int() && T.bits == 32);
}

class Simplify : public IRMutator {
public:
    Simplify(bool r, const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai) :
        simplify_lets(r) {
        alignment_info.set_containing_scope(ai);
        bounds_info.set_containing_scope(bi);
    }

    // Uncomment to debug all Expr mutations.
    /*
    Expr mutate(Expr e) {
        Expr new_e = IRMutator::mutate(e);
        debug(0) << e << " -> " << new_e << "\n";
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
    Scope<Interval> bounds_info;
    Scope<ModulusRemainder> alignment_info;

    using IRMutator::visit;

    bool const_float(Expr e, float *f) {
        if (!e.defined()) {
            return false;
        }
        const FloatImm *c = e.as<FloatImm>();
        if (c) {
            *f = c->value;
            return true;
        } else {
            return false;
        }
    }

    bool const_int(Expr e, int *i) {
        if (!e.defined()) {
            return false;
        }
        const IntImm *c = e.as<IntImm>();
        if (c) {
            *i = c->value;
            return true;
        } else {
            return false;
        }
    }

    Expr is_round_up(Expr e, int *factor) {
        const Mul *mul = e.as<Mul>();
        if (!mul) return Expr();
        if (!const_int(mul->b, factor)) return Expr();
        const Div *div = mul->a.as<Div>();
        if (!div) return Expr();
        if (!is_const(div->b, *factor)) return Expr();
        const Add *add = div->a.as<Add>();
        if (!add) return Expr();
        if (!is_const(add->b, (*factor)-1)) return Expr();
        return add->a;
    }

    /* Recognise an integer or cast integer and fetch its value.
     * Only matches if the number of bits of the cast integer does not exceed
     * the number of bits of an int in the compiler, because simplification
     * uses type int for its calculations. */
    bool const_castint(Expr e, int *i) {
        const IntImm *intimm = e.as<IntImm>();
        const Cast *cast = e.as<Cast>();
        const int max_bits = (int)(sizeof(int) * 8);
        if (intimm) {
            *i = intimm->value;
            return true;
        } else if (cast &&
                   ((cast->type.is_int() && cast->type.bits <= max_bits) ||
                    (cast->type.is_uint() && cast->type.bits <= max_bits)) &&
                   const_castint(cast->value, i)) {
            // When fetching a cast integer, ensure that the
            // return value is in the correct range (i.e. the
            // canonical value) for the cast type.
            *i = int_cast_constant(cast->type, *i);
            return true;
        } else {
            return false;
        }
    }

    void visit(const Cast *op) {
        Expr value = mutate(op->value);
        const Cast *cast = value.as<Cast>();
        const Broadcast *broadcast_value = value.as<Broadcast>();
        float f = 0.0f;
        int i = 0;
        if (value.type() == op->type) {
            expr = value;
        } else if (op->type == Int(32) && const_float(value, &f)) {
            expr = IntImm::make((int)f);
        } else if (op->type == Float(32) && const_int(value, &i)) {
            expr = FloatImm::make((float)i);
        } else if (cast && op->type.code == cast->type.code && op->type.bits < cast->type.bits) {
            // If this is a cast of a cast of the same type, where the
            // outer cast is narrower, the inner cast can be
            // eliminated.
            expr = mutate(Cast::make(op->type, cast->value));
        } else if (op->type.bits < 64 && cast && const_castint(cast->value, &i)) {
            // cast of cast of const int can just be cast of const int
            // (with the int suitably munged to fit in the
            // intermediate type). We can't do the munging correctly
            // when the result has > 32 bits.
            // u16(u8(255)) -> u16(255)
            // u16(u8(257)) -> u16(1)
            expr = mutate(Cast::make(op->type, do_indirect_int_cast(cast->type, i)));
        } else if (op->type == Int(32) && cast && const_int(cast->value, &i)) {
            // Cast to something then back to int
            expr = do_indirect_int_cast(cast->type, i);
        } else if (!op->type.is_float() &&
                   op->type.bits <= 32 &&
                   const_int(value, &i) &&
                   do_indirect_int_cast(op->type, i) != i) {
            // Rewrite things like cast(UInt(8), 256) to cast(UInt(8),
            // 0), so any later peephole matching that ignores casts
            // doesn't get confused.
            expr = Cast::make(op->type, do_indirect_int_cast(op->type, i));
        } else if (broadcast_value) {
            // cast(broadcast(x)) -> broadcast(cast(x))
            expr = mutate(Broadcast::make(Cast::make(op->type.element_of(), broadcast_value->value), broadcast_value->width));
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
        int ia = 0, ib = 0, ic = 0;
        float fa = 0.0f, fb = 0.0f;

        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        // rearrange const + varying to varying + const, to cut down
        // on cases to check
        if (is_simple_const(a) && !is_simple_const(b)) std::swap(a, b);

        // Rearrange a + min or a + max to min + a or max + a to cut down on cases to check
        if (b.as<Min>() || b.as<Max>()) {
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

        const Div *div_a_a = mul_a ? mul_a->a.as<Div>() : NULL;
        const Mod *mod_a = a.as<Mod>();
        const Mod *mod_b = b.as<Mod>();

        const Mul *mul_a_a = add_a ? add_a->a.as<Mul>(): NULL;
        const Mod *mod_a_a = add_a ? add_a->a.as<Mod>(): NULL;
        const Mul *mul_a_b = add_a ? add_a->b.as<Mul>(): NULL;
        const Mod *mod_a_b = add_a ? add_a->b.as<Mod>(): NULL;

        const Min *min_a = a.as<Min>();
        const Max *max_a = a.as<Max>();
        const Sub *sub_a_a = min_a ? min_a->a.as<Sub>() : NULL;
        const Sub *sub_a_b = min_a ? min_a->b.as<Sub>() : NULL;
        const Add *add_a_a = min_a ? min_a->a.as<Add>() : NULL;
        const Add *add_a_b = min_a ? min_a->b.as<Add>() : NULL;
        sub_a_a = max_a ? max_a->a.as<Sub>() : sub_a_a;
        sub_a_b = max_a ? max_a->b.as<Sub>() : sub_a_b;
        add_a_a = max_a ? max_a->a.as<Add>() : add_a_a;
        add_a_b = max_a ? max_a->b.as<Add>() : add_a_b;

        add_a_a = div_a ? div_a->a.as<Add>() : add_a_a;

        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();

        if (const_int(a, &ia) &&
            const_int(b, &ib)) {
            expr = ia + ib;
            // const int + const int
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb)) {
            // const float + const float
            expr = fa + fb;
        } else if (is_zero(b)) {
            expr = a;
        } else if (is_zero(a)) {
            expr = b;
        } else if (const_castint(a, &ia) && const_castint(b, &ib)) {
            if (op->type.is_uint()) {
                expr = make_const(op->type, ((unsigned int) ia) + ((unsigned int) ib));
            } else {
                expr = make_const(op->type, ia + ib);
            }
        } else if (ramp_a && ramp_b) {
            // Ramp + Ramp
            expr = mutate(Ramp::make(ramp_a->base + ramp_b->base,
                                   ramp_a->stride + ramp_b->stride, ramp_a->width));
        } else if (ramp_a && broadcast_b) {
            // Ramp + Broadcast
            expr = mutate(Ramp::make(ramp_a->base + broadcast_b->value,
                                   ramp_a->stride, ramp_a->width));
        } else if (broadcast_a && ramp_b) {
            // Broadcast + Ramp
            expr = mutate(Ramp::make(broadcast_a->value + ramp_b->base,
                                   ramp_b->stride, ramp_b->width));
        } else if (broadcast_a && broadcast_b) {
            // Broadcast + Broadcast
            expr = Broadcast::make(mutate(broadcast_a->value + broadcast_b->value),
                                 broadcast_a->width);

        } else if (select_a && select_b &&
                   equal(select_a->condition, select_b->condition)) {
            // select(c, a, b) + select(c, d, e) -> select(c, a+d, b+e)
            expr = mutate(Select::make(select_a->condition,
                                       select_a->true_value + select_b->true_value,
                                       select_a->false_value + select_b->false_value));
        } else if (add_a && is_simple_const(add_a->b)) {
            // In ternary expressions, pull constants outside
            if (is_simple_const(b)) {
                expr = mutate(add_a->a + (add_a->b + b));
            } else {
                expr = mutate((add_a->a + b) + add_a->b);
            }
        } else if (add_b && is_simple_const(add_b->b)) {
            expr = mutate((a + add_b->a) + add_b->b);
        } else if (sub_a && is_simple_const(sub_a->a)) {
            if (is_simple_const(b)) {
                expr = mutate((sub_a->a + b) - sub_a->b);
            } else {
                expr = mutate((b - sub_a->b) + sub_a->a);
            }

        } else if (sub_a && equal(b, sub_a->b)) {
            // Additions that cancel an inner term
            // (a - b) + b
            expr = sub_a->a;
        } else if (sub_a && is_zero(sub_a->a)) {
            expr = mutate(b - sub_a->b);
        } else if (sub_b && equal(a, sub_b->b)) {
            // a + (b - a)
            expr = sub_b->a;
        } else if (sub_b && is_simple_const(sub_b->a)) {
            // a + (7 - b) -> (a - b) + 7
            expr = mutate((a - sub_b->b) + sub_b->a);
        } else if (sub_a && sub_b && equal(sub_a->b, sub_b->a)) {
            // (a - b) + (b - c) -> a - c
            expr = mutate(sub_a->a - sub_b->b);
        } else if (sub_a && sub_b && equal(sub_a->a, sub_b->b)) {
            // (a - b) + (c - a) -> c - b
            expr = mutate(sub_b->a - sub_a->b);
        } else if (mul_b && is_negative_negatable_const(mul_b->b)) {
            // a + b*-x -> a - b*x
            expr = mutate(a - mul_b->a * (-mul_b->b));
        } else if (mul_a && is_negative_negatable_const(mul_a->b)) {
            // a*-x + b -> b - a*x
            expr = mutate(b - mul_a->a * (-mul_a->b));
        } else if (min_a && sub_a_b && no_overflow(op->type) && equal(sub_a_b->b, b)) {
            // min(a, b-c) + c -> min(a+c, b)
            expr = mutate(Min::make(Add::make(min_a->a, b), sub_a_b->a));
        } else if (min_a && sub_a_a && no_overflow(op->type) && equal(sub_a_a->b, b)) {
            // min(a-c, b) + c -> min(a, b+c)
            expr = mutate(Min::make(sub_a_a->a, Add::make(min_a->b, b)));
        } else if (max_a && sub_a_b && no_overflow(op->type) && equal(sub_a_b->b, b)) {
            // max(a, b-c) + c -> max(a+c, b)
            expr = mutate(Max::make(Add::make(max_a->a, b), sub_a_b->a));
        } else if (max_a && sub_a_a && no_overflow(op->type) && equal(sub_a_a->b, b)) {
            // max(a-c, b) + c -> max(a, b+c)
            expr = mutate(Max::make(sub_a_a->a, Add::make(max_a->b, b)));

        } else if (min_a && add_a_b && no_overflow(op->type) && const_int(add_a_b->b, &ia) && const_int(b, &ib) && ia + ib == 0) {
            // min(a, b + (-2)) + 2 -> min(a + 2, b)
            expr = mutate(Min::make(Add::make(min_a->a, b), add_a_b->a));
        } else if (min_a && add_a_a && no_overflow(op->type) && const_int(add_a_a->b, &ia) && const_int(b, &ib) && ia + ib == 0) {
            // min(a + (-2), b) + 2 -> min(a, b + 2)
            expr = mutate(Min::make(add_a_a->a, Add::make(min_a->b, b)));
        } else if (max_a && add_a_b && no_overflow(op->type) && const_int(add_a_b->b, &ia) && const_int(b, &ib) && ia + ib == 0) {
            // max(a, b + (-2)) + 2 -> max(a + 2, b)
            expr = mutate(Max::make(Add::make(max_a->a, b), add_a_b->a));
        } else if (max_a && add_a_a && no_overflow(op->type) && const_int(add_a_a->b, &ia) && const_int(b, &ib) && ia + ib == 0) {
            // max(a + (-2), b) + 2 -> max(a, b + 2)
            expr = mutate(Max::make(add_a_a->a, Add::make(max_a->b, b)));
        } else if (div_a && add_a_a &&
                   const_int(add_a_a->b, &ia) &&
                   const_int(div_a->b, &ib) && ib &&
                   const_int(b, &ic)) {
            // ((a + ia) / ib + ic) -> (a + (ia + ib*ic)) / ib
            expr = mutate((add_a_a->a + (ia + ib*ic)) / ib);
        } else if (mul_a && mul_b && equal(mul_a->a, mul_b->a)) {
            // Pull out common factors a*x + b*x
            expr = mutate(mul_a->a * (mul_a->b + mul_b->b));
        } else if (mul_a && mul_b && equal(mul_a->b, mul_b->a)) {
            expr = mutate(mul_a->b * (mul_a->a + mul_b->b));
        } else if (mul_a && mul_b && equal(mul_a->b, mul_b->b)) {
            expr = mutate(mul_a->b * (mul_a->a + mul_b->a));
        } else if (mul_a && mul_b && equal(mul_a->a, mul_b->b)) {
            expr = mutate(mul_a->a * (mul_a->b + mul_b->a));
        } else if (mod_a && mul_b && equal(mod_a->b, mul_b->b)) {
            // (x%3) + y*3 -> y*3 + x%3
            expr = mutate(b + a);
        } else if (mul_a && mod_b && div_a_a &&
                   equal(mul_a->b, div_a_a->b) &&
                   equal(mul_a->b, mod_b->b) &&
                   equal(div_a_a->a, mod_b->a)) {
            // (x/3)*3 + x%3 -> x
            expr = div_a_a->a;
        } else if (add_a && mul_a_a && mod_b &&
                   equal(mul_a_a->b, mod_b->b) &&
                   (!mod_a_b || !equal(mod_a_b->b, mod_b->b))) {
            // ((x*3) + y) + z%3 -> (x*3 + z%3) + y
            expr = mutate((add_a->a + b) + add_a->b);
        } else if (add_a && mod_a_a && mul_b &&
                   equal(mod_a_a->b, mul_b->b) &&
                   (!mod_a_b || !equal(mod_a_b->b, mul_b->b))) {
            // ((x%3) + y) + z*3 -> (z*3 + x%3) + y
            expr = mutate((b + add_a->a) + add_a->b);
        } else if (add_a && mul_a_b && mod_b &&
                   equal(mul_a_b->b, mod_b->b) &&
                   (!mod_a_a || !equal(mod_a_a->b, mod_b->b))) {
            // (y + (x*3)) + z%3 -> y + (x*3 + z%3)
            expr = mutate(add_a->a + (add_a->b + b));
        } else if (add_a && mod_a_b && mul_b &&
                   equal(mod_a_b->b, mul_b->b) &&
                   (!mod_a_a || !equal(mod_a_a->b, mul_b->b))) {
            // (y + (x%3)) + z*3 -> y + (z*3 + x%3)
            expr = mutate(add_a->a + (b + add_a->b));
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

        int ia = 0, ib = 0;
        float fa = 0.0f, fb = 0.0f;

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

        const Min *min_b = b.as<Min>();
        const Add *add_b_a = min_b ? min_b->a.as<Add>() : NULL;
        const Add *add_b_b = min_b ? min_b->b.as<Add>() : NULL;

        const Min *min_a = a.as<Min>();
        const Add *add_a_a = min_a ? min_a->a.as<Add>() : NULL;
        const Add *add_a_b = min_a ? min_a->b.as<Add>() : NULL;

        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();

        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();

        if (is_zero(b)) {
            expr = a;
        } else if (equal(a, b)) {
            expr = make_zero(op->type);
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            expr = ia - ib;
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = fa - fb;
        } else if (const_int(b, &ib)) {
            expr = mutate(a + (-ib));
        } else if (const_float(b, &fb)) {
            expr = mutate(a + (-fb));
        } else if (const_castint(a, &ia) && const_castint(b, &ib)) {
            if (op->type.is_uint()) {
                expr = make_const(op->type, ((unsigned int) ia) - ((unsigned int) ib));
            } else {
                expr = make_const(op->type, ia - ib);
            }
        } else if (ramp_a && ramp_b) {
            // Ramp - Ramp
            expr = mutate(Ramp::make(ramp_a->base - ramp_b->base,
                                   ramp_a->stride - ramp_b->stride, ramp_a->width));
        } else if (ramp_a && broadcast_b) {
            // Ramp - Broadcast
            expr = mutate(Ramp::make(ramp_a->base - broadcast_b->value,
                                   ramp_a->stride, ramp_a->width));
        } else if (broadcast_a && ramp_b) {
            // Broadcast - Ramp
            expr = mutate(Ramp::make(broadcast_a->value - ramp_b->base,
                                   make_zero(ramp_b->stride.type())- ramp_b->stride,
                                   ramp_b->width));
        } else if (broadcast_a && broadcast_b) {
            // Broadcast + Broadcast
            expr = Broadcast::make(mutate(broadcast_a->value - broadcast_b->value),
                                 broadcast_a->width);
        } else if (select_a && select_b &&
                   equal(select_a->condition, select_b->condition)) {
            // select(c, a, b) - select(c, d, e) -> select(c, a+d, b+e)
            expr = mutate(Select::make(select_a->condition,
                                       select_a->true_value - select_b->true_value,
                                       select_a->false_value - select_b->false_value));
        } else if (add_a && equal(add_a->b, b)) {
            // Ternary expressions where a term cancels
            expr = add_a->a;
        } else if (add_a && equal(add_a->a, b)) {
            expr = add_a->b;
        } else if (add_b && equal(add_b->b, a)) {
            expr = mutate(make_zero(add_b->a.type()) - add_b->a);
        } else if (add_b && equal(add_b->a, a)) {
            expr = mutate(make_zero(add_b->a.type()) - add_b->b);
        } else if (add_a && is_simple_const(add_a->b)) {
            // In ternary expressions, pull constants outside
            if (is_simple_const(b)) {
                expr = mutate(add_a->a + (add_a->b - b));
            } else {
                expr = mutate((add_a->a - b) + add_a->b);
            }
        } else if (sub_a && sub_b && is_const(sub_a->a) && is_const(sub_b->a)) {
            // (c1 - a) - (c2 - b) -> (b - a) + (c1 - c2)
            expr = mutate((sub_b->b - sub_a->b) + (sub_a->a - sub_b->a));
        } else if (sub_b) {
            // a - (b - c) -> a + (c - b)
            expr = mutate(a + (sub_b->b - sub_b->a));
        } else if (mul_b && is_negative_negatable_const(mul_b->b)) {
            // a - b*-x -> a + b*x
            expr = mutate(a + mul_b->a * (-mul_b->b));
        } else if (add_b && is_simple_const(add_b->b)) {
            expr = mutate((a - add_b->a) - add_b->b);
        } else if (sub_a && is_simple_const(sub_a->a) && is_simple_const(b)) {
            expr = mutate((sub_a->a - b) - sub_a->b);
        } else if (mul_a && mul_b && equal(mul_a->a, mul_b->a)) {
            // Pull out common factors a*x + b*x
            expr = mutate(mul_a->a * (mul_a->b - mul_b->b));
        } else if (mul_a && mul_b && equal(mul_a->b, mul_b->a)) {
            expr = mutate(mul_a->b * (mul_a->a - mul_b->b));
        } else if (mul_a && mul_b && equal(mul_a->b, mul_b->b)) {
            expr = mutate(mul_a->b * (mul_a->a - mul_b->a));
        } else if (mul_a && mul_b && equal(mul_a->a, mul_b->b)) {
            expr = mutate(mul_a->a * (mul_a->b - mul_b->a));
        } else if (add_a && add_b && equal(add_a->b, add_b->b)) {
            // Quaternary expressions where a term cancels
            // (a + b) - (c + b) -> a - c
            expr = mutate(add_a->a - add_b->a);
        } else if (add_a && add_b && equal(add_a->a, add_b->a)) {
            // (a + b) - (a + c) -> b - c
            expr = mutate(add_a->b - add_b->b);
        } else if (add_a && add_b && equal(add_a->a, add_b->b)) {
            // (a + b) - (c + a) -> b - c
            expr = mutate(add_a->b - add_b->a);
        } else if (add_a && add_b && equal(add_a->b, add_b->a)) {
            // (b + a) - (a + c) -> b - c
            expr = mutate(add_a->a - add_b->b);
        } else if (min_b && add_b_a && no_overflow(op->type) && equal(a, add_b_a->a)) {
            // Quaternary expressions involving mins where a term
            // cancels. These are important for bounds inference
            // simplifications.
            // a - min(a + b, c) -> max(-b, a-c)
            expr = mutate(max(0 - add_b_a->b, a - min_b->b));
        } else if (min_b && add_b_a && no_overflow(op->type) && equal(a, add_b_a->b)) {
            // a - min(b + a, c) -> max(-b, a-c)
            expr = mutate(max(0 - add_b_a->a, a - min_b->b));
        } else if (min_b && add_b_b && no_overflow(op->type) && equal(a, add_b_b->a)) {
            // a - min(c, a + b) -> max(-b, a-c)
            expr = mutate(max(0 - add_b_b->b, a - min_b->a));
        } else if (min_b && add_b_b && no_overflow(op->type) && equal(a, add_b_b->b)) {
            // a - min(c, b + a) -> max(-b, a-c)
            expr = mutate(max(0 - add_b_b->a, a - min_b->a));
        } else if (min_a && add_a_a && no_overflow(op->type) && equal(b, add_a_a->a)) {
            // min(a + b, c) - a -> min(b, c-a)
            expr = mutate(min(add_a_a->b, min_a->b - b));
        } else if (min_a && add_a_a && no_overflow(op->type) && equal(b, add_a_a->b)) {
            // min(b + a, c) - a -> min(b, c-a)
            expr = mutate(min(add_a_a->a, min_a->b - b));
        } else if (min_a && add_a_b && no_overflow(op->type) && equal(b, add_a_b->a)) {
            // min(c, a + b) - a -> min(b, c-a)
            expr = mutate(min(add_a_b->b, min_a->a - b));
        } else if (min_a && add_a_b && no_overflow(op->type) && equal(b, add_a_b->b)) {
            // min(c, b + a) - a -> min(b, c-a)
            expr = mutate(min(add_a_b->a, min_a->a - b));
        } else if (min_a && min_b && equal(min_a->a, min_b->b) && equal(min_a->b, min_b->a)) {
            // min(a, b) - min(b, a) -> 0
            expr = make_zero(op->type);
        } else if (max_a && max_b && equal(max_a->a, max_b->b) && equal(max_a->b, max_b->a)) {
            // max(a, b) - max(b, a) -> 0
            expr = make_zero(op->type);
        } else if (min_a && min_b && is_zero(simplify((min_a->a + min_b->b) - (min_a->b + min_b->a)))) {
            // min(a, b) - min(c, d) where a-b == c-d -> b - d
            expr = mutate(min_a->b - min_b->b);
        } else if (max_a && max_b && is_zero(simplify((max_a->a + max_b->b) - (max_a->b + max_b->a)))) {
            // max(a, b) - max(c, d) where a-b == c-d -> b - d
            expr = mutate(max_a->b - max_b->b);
        } else if (min_a && min_b && is_zero(simplify((min_a->a + min_b->a) - (min_a->b + min_b->b)))) {
            // min(a, b) - min(c, d) where a-b == d-c -> b - c
            expr = mutate(min_a->b - min_b->a);
        } else if (max_a && max_b && is_zero(simplify((max_a->a + max_b->a) - (max_a->b + max_b->b)))) {
            // max(a, b) - max(c, d) where a-b == d-c -> b - c
            expr = mutate(max_a->b - max_b->a);
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

        int ia = 0, ib = 0;
        float fa = 0.0f, fb = 0.0f;

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Mul *mul_a = a.as<Mul>();

        if (is_zero(a)) {
            expr = a;
        } else if (is_zero(b)) {
            expr = b;
        } else if (is_one(a)) {
            expr = b;
        } else if (is_one(b)) {
            expr = a;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            expr = ia*ib;
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = fa*fb;
        } else if (const_castint(a, &ia) && const_castint(b, &ib)) {
            if (op->type.is_uint()) {
                expr = make_const(op->type, ((unsigned int) ia) * ((unsigned int) ib));
            } else {
                expr = make_const(op->type, ia * ib);
            }
        } else if (broadcast_a && broadcast_b) {
            expr = Broadcast::make(mutate(broadcast_a->value * broadcast_b->value), broadcast_a->width);
        } else if (ramp_a && broadcast_b) {
            Expr m = broadcast_b->value;
            expr = mutate(Ramp::make(ramp_a->base * m, ramp_a->stride * m, ramp_a->width));
        } else if (broadcast_a && ramp_b) {
            Expr m = broadcast_a->value;
            expr = mutate(Ramp::make(m * ramp_b->base, m * ramp_b->stride, ramp_b->width));
        } else if (add_a && !(add_a->b.as<Ramp>() && ramp_b) && is_simple_const(add_a->b) && is_simple_const(b)) {
            expr = mutate(add_a->a * b + add_a->b * b);
        } else if (sub_a && is_negative_negatable_const(b)) {
            expr = mutate(Mul::make(Sub::make(sub_a->b, sub_a->a), -b));
        } else if (mul_a && is_simple_const(mul_a->b) && is_simple_const(b)) {
            expr = mutate(mul_a->a * (mul_a->b * b));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Mul::make(a, b);
        }
    }

    void visit(const Div *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        int ia = 0, ib = 0, ic = 0;
        float fa = 0.0f, fb = 0.0f;

        const Mul *mul_a = a.as<Mul>();
        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Div *div_a = a.as<Div>();
        const Div *div_a_a = NULL;
        const Mul *mul_a_a = NULL;
        const Mul *mul_a_b = NULL;
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

        if (op->type == Int(32) && const_int(b, &ib) && ib && !is_const(a)) {
            // Check for bounded numerators divided by constant
            // denominators.
            Interval bounds = bounds_of_expr_in_scope(a, bounds_info);
            if (bounds.min.defined() &&
                bounds.max.defined()) {
                bounds.min = mutate(bounds.min);
                bounds.max = mutate(bounds.max);
                int num_min = 0, num_max = 0;
                if (const_int(bounds.min, &num_min) &&
                    const_int(bounds.max, &num_max) &&
                    div_imp(num_max, ib) == div_imp(num_min, ib)) {
                    expr = div_imp(num_max, ib);
                    return;
                }
            }
        }

        ModulusRemainder mod_rem(0, 1);
        if (ramp_a && ramp_a->base.type() == Int(32)) {
            // Do modulus remainder analysis on the base.
            mod_rem = modulus_remainder(ramp_a->base, alignment_info);
        }

        if (is_zero(a) && !is_zero(b)) {
            expr = a;
        } else if (is_one(b)) {
            expr = a;
        } else if (equal(a, b) && !is_zero(b)) {
            expr = make_one(a.type());
        } else if (const_int(a, &ia) && const_int(b, &ib) && ib) {
            expr = div_imp(ia,ib);
        } else if (const_float(a, &fa) && const_float(b, &fb) && fb != 0.0f) {
            expr = fa/fb;
        } else if (const_castint(a, &ia) && const_castint(b, &ib) && ib) {
            if (op->type.is_uint()) {
                expr = make_const(op->type, ((unsigned int)ia)/((unsigned int)ib));
            } else {
                expr = make_const(op->type, div_imp(ia,ib)); //Use the definition::make of division
            }
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(Broadcast::make(Div::make(broadcast_a->value, broadcast_b->value), broadcast_a->width));
        } else if (ramp_a && const_int(ramp_a->stride, &ia) &&
                   broadcast_b && const_int(broadcast_b->value, &ib) && ib &&
                   ia % ib == 0) {
            // ramp(x, 4, w) / broadcast(2, w) -> ramp(x / 2, 2, w)
            expr = mutate(Ramp::make(ramp_a->base / ib, div_imp(ia, ib), ramp_a->width));
        } else if (ramp_a && ramp_a->base.type() == Int(32) && const_int(ramp_a->stride, &ia) &&
                   broadcast_b && const_int(broadcast_b->value, &ib) && ib != 0 &&
                   mod_rem.modulus % ib == 0 &&
                   div_imp(mod_rem.remainder, ib) == div_imp(mod_rem.remainder + (ramp_a->width-1)*ia, ib)) {
            // ramp(k*z + x, y, w) / z = broadcast(k, w) if x/z == (x + (w-1)*y)/z
            expr = mutate(Broadcast::make(ramp_a->base / ib, ramp_a->width));
        } else if (div_a &&
                   const_int(div_a->b, &ia) && ia >= 0 &&
                   const_int(b, &ib) && ib >= 0) {
            // (x / 3) / 4 -> x / 12
            expr = mutate(Div::make(div_a->a, ia*ib));
        } else if (div_a_a && add_a &&
                   const_int(div_a_a->b, &ia) && ia >= 0 &&
                   const_int(add_a->b, &ib) &&
                   const_int(b, &ic) && ic >= 0) {
            // (x / ia + ib) / ic -> (x + ia*ib) / (ia*ic)
            expr = mutate(Div::make(div_a_a->a + ia*ib, ia*ic));
        } else if (mul_a && const_int(mul_a->b, &ia) && const_int(b, &ib) &&
                   ia > 0 && ib > 0 && (ia % ib == 0 || ib % ia == 0)) {
            if (ia % ib == 0) {
                // (x * 4) / 2 -> x * 2
                expr = mutate(mul_a->a * div_imp(ia, ib));
            } else {
                // (x * 2) / 4 -> x / 2
                expr = mutate(mul_a->a / div_imp(ib, ia));
            }
        } else if (add_a && mul_a_a && const_int(mul_a_a->b, &ia) && const_int(b, &ib) &&
                   ib > 0 && (ia % ib == 0)) {
            // Pull terms that are a multiple of the divisor out
            // (x*4 + y) / 2 -> x*2 + y/2
            expr = mutate((mul_a_a->a * div_imp(ia, ib)) + (add_a->b / b));
        } else if (add_a && mul_a_b && const_int(mul_a_b->b, &ia) && const_int(b, &ib) &&
                   ib > 0 && (ia % ib == 0)) {
            // (y + x*4) / 2 -> y/2 + x*2
            expr = mutate((add_a->a / b) + (mul_a_b->a * div_imp(ia, ib)));
        } else if (sub_a && mul_a_a && const_int(mul_a_a->b, &ia) && const_int(b, &ib) &&
                   ib > 0 && (ia % ib == 0)) {
            // Pull terms that are a multiple of the divisor out
            // (x*4 - y) / 2 -> x*2 - y/2
            expr = mutate((mul_a_a->a * div_imp(ia, ib)) - (sub_a->b / b));
        } else if (sub_a && mul_a_b && const_int(mul_a_b->b, &ia) && const_int(b, &ib) &&
                   ib > 0 && (ia % ib == 0)) {
            // (y - x*4) / 2 -> y/2 - x*2
            expr = mutate((sub_a->a / b) - (mul_a_b->a * div_imp(ia, ib)));
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

        int ia = 0, ib = 0;
        float fa = 0.0f, fb = 0.0f;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Mul *mul_a = a.as<Mul>();
        const Add *add_a = a.as<Add>();
        const Mul *mul_a_a = add_a ? add_a->a.as<Mul>() : NULL;
        const Mul *mul_a_b = add_a ? add_a->b.as<Mul>() : NULL;
        const Ramp *ramp_a = a.as<Ramp>();

        // If the RHS is a constant, do modulus remainder analysis on the LHS
        ModulusRemainder mod_rem(0, 1);
        if (const_int(b, &ib) && ib && a.type() == Int(32)) {
            // If the LHS is bounded, we can possibly bail out early
            Interval ia = bounds_of_expr_in_scope(a, bounds_info);
            if (ia.max.defined() && ia.min.defined() &&
                is_one(mutate((ia.max < b) && (ia.min >= 0)))) {
                expr = a;
                return;
            }

            mod_rem = modulus_remainder(a, alignment_info);
        }

        // If the RHS is a constant and the LHS is a ramp, do modulus
        // remainder analysis on the base.
        if (broadcast_b &&
            const_int(broadcast_b->value, &ib) && ib &&
            ramp_a && ramp_a->base.type() == Int(32)) {
            mod_rem = modulus_remainder(ramp_a->base, alignment_info);
        }

        if (is_zero(a) && !is_zero(b)) {
            expr = a;
        } else if (const_int(a, &ia) && const_int(b, &ib) && ib) {
            expr = mod_imp(ia, ib);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = mod_imp(fa, fb);
        } else if (const_castint(a, &ia) && const_castint(b, &ib) && ib) {
            if (op->type.is_uint()) {
                expr = make_const(op->type, ((unsigned int)ia) % ((unsigned int)ib));
            } else {
                expr = Cast::make(op->type, mod_imp(ia, ib));
            }
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(Broadcast::make(Mod::make(broadcast_a->value, broadcast_b->value), broadcast_a->width));
        } else if (mul_a && const_int(b, &ib) && ib && const_int(mul_a->b, &ia) && (ia % ib == 0)) {
            // (x * (b*a)) % b -> 0
            expr = make_zero(a.type());
        } else if (add_a && mul_a_a && const_int(mul_a_a->b, &ia) && const_int(b, &ib) && ib && (ia % ib == 0)) {
            // (x * (b*a) + y) % b -> (y % b)
            expr = mutate(add_a->b % ib);
        } else if (add_a && const_int(add_a->b, &ia) && const_int(b, &ib) && ib && (ia % ib == 0)) {
            // (y + (b*a)) % b -> (y % b)
            expr = mutate(add_a->a % ib);
        } else if (add_a && mul_a_b && const_int(mul_a_b->b, &ia) && const_int(b, &ib) && ib && (ia % ib == 0)) {
            // (y + x * (b*a)) % b -> (y % b)
            expr = mutate(add_a->a % ib);
        } else if (const_int(b, &ib) && ib && a.type() == Int(32) && mod_rem.modulus % ib == 0) {
            // ((a*b)*x + c) % a -> c % a
            expr = mod_imp(mod_rem.remainder, ib);
        } else if (ramp_a && const_int(ramp_a->stride, &ia) &&
                   broadcast_b && const_int(broadcast_b->value, &ib) && ib &&
                   ia % ib == 0) {
            // ramp(x, 4, w) % broadcast(2, w)
            expr = mutate(Broadcast::make(ramp_a->base % ib, ramp_a->width));
        } else if (ramp_a && ramp_a->base.type() == Int(32) && const_int(ramp_a->stride, &ia) &&
                   broadcast_b && const_int(broadcast_b->value, &ib) && ib != 0 &&
                   mod_rem.modulus % ib == 0 &&
                   div_imp(mod_rem.remainder, ib) == div_imp(mod_rem.remainder + (ramp_a->width-1)*ia, ib)) {
            // ramp(k*z + x, y, w) % z = ramp(x, y, w) if x/z == (x + (w-1)*y)/z
            expr = mutate(Ramp::make(mod_imp(mod_rem.remainder, ib), ramp_a->stride, ramp_a->width));
        } else if (ramp_a && ramp_a->base.type() == Int(32) &&
                   const_int(ramp_a->stride, &ia) && !is_const(ramp_a->base) &&
                   broadcast_b && const_int(broadcast_b->value, &ib) && ib != 0 &&
                   mod_rem.modulus % ib == 0) {
            // ramp(k*z + x, y, w) % z = ramp(x, y, w) % z
            expr = mutate(Ramp::make(mod_imp(mod_rem.remainder, ib), ramp_a->stride, ramp_a->width) % ib);
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

        int ia = 0, ib = 0, ic = 0;
        float fa = 0.0f, fb = 0.0f;
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
        const Min *min_a_a = min_a ? min_a->a.as<Min>() : NULL;
        const Min *min_a_a_a = min_a_a ? min_a_a->a.as<Min>() : NULL;
        const Min *min_a_a_a_a = min_a_a_a ? min_a_a_a->a.as<Min>() : NULL;
        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();
        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();

        min_a_a = max_a ? max_a->a.as<Min>() : min_a_a;

        // Detect if the lhs or rhs is a rounding-up operation
        int a_round_up_factor = 0, b_round_up_factor = 0;
        Expr a_round_up = is_round_up(a, &a_round_up_factor);
        Expr b_round_up = is_round_up(b, &b_round_up_factor);

        if (equal(a, b)) {
            expr = a;
            return;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            expr = std::min(ia, ib);
            return;
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = std::min(fa, fb);
            return;
        } else if (const_castint(a, &ia) && const_castint(b, &ib)) {
            if (op->type.is_uint()) {
                expr = make_const(op->type, std::min(((unsigned int) ia), ((unsigned int) ib)));
            } else {
                expr = make_const(op->type, std::min(ia,ib));
            }
            return;
        } else if (const_castint(b, &ib) && ib == b.type().imax()) {
            // Compute minimum of expression of type and maximum of type --> expression
            expr = a;
            return;
        } else if (const_castint(b, &ib) && ib == b.type().imin()) {
            // Compute minimum of expression of type and minimum of type --> min of type
            expr = b;
            return;
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(Broadcast::make(Min::make(broadcast_a->value, broadcast_b->value), broadcast_a->width));
            return;
        } else if (op->type == Int(32) && a.as<Variable>() && is_simple_const(b)) {
            Expr delta = mutate(a - b);
            Interval id = bounds_of_expr_in_scope(delta, bounds_info);
            id.min = mutate(id.min);
            id.max = mutate(id.max);
            if (id.min.defined() && (is_zero(id.min) || is_positive_const(id.min))) {
                expr = b;
                return;
            } else if (id.max.defined() && (is_zero(id.max) || is_negative_const(id.max))) {
                expr = a;
                return;
            }
       } else if (ramp_a && broadcast_b &&
                  const_int(ramp_a->base, &ia) &&
                  const_int(ramp_a->stride, &ib) &&
                  const_int(broadcast_b->value, &ic)) {
            // min(ramp(a, b, n), broadcast(c, n))
            int ramp_start = ia;
            int ramp_end = ia + ib * (ramp_a->width - 1);
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

        if (add_a && const_int(add_a->b, &ia) &&
            add_b && const_int(add_b->b, &ib) &&
            equal(add_a->a, add_b->a)) {
            // min(x + 3, x - 2) -> x - 2
            if (ia > ib) {
                expr = b;
            } else {
                expr = a;
            }
        } else if (add_a && const_int(add_a->b, &ia) && equal(add_a->a, b)) {
            // min(x + 5, x) -> x
            if (ia > 0) {
                expr = b;
            } else {
                expr = a;
            }
        } else if (add_b && const_int(add_b->b, &ib) && equal(add_b->a, a)) {
            // min(x, x + 5) -> x
            if (ib > 0) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (sub_a && sub_b && equal(sub_a->b, sub_b->b) &&
                   const_int(sub_a->a, &ia) && const_int(sub_b->a, &ib)) {
            // min (100-x, 101-x) -> 100-x
            if (ia < ib) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (a_round_up.defined() && equal(a_round_up, b)) {
            // min(((a + 3)/4)*4, a) -> a
            expr = b;
        } else if (a_round_up.defined() && max_b &&
                   equal(a_round_up, max_b->a) && equal(a_round_up_factor, max_b->b)) {
            // min(((a + 3)/4)*4, max(a, 4)) -> max(a, 4)
            expr = b;
        } else if (b_round_up.defined() && equal(b_round_up, a)) {
            // min(a, ((a + 3)/4)*4) -> a
            expr = a;
        } else if (b_round_up.defined() && max_a &&
                   equal(b_round_up, max_a->a) && equal(b_round_up_factor, max_a->b)) {
            // min(max(a, 4), ((a + 3)/4)*4) -> max(a, 4)
            expr = a;
        } else if (max_a && equal(max_a->b, b)) {
            // min(max(x, y), y) -> y
            expr = b;
        } else if (min_a && (equal(min_a->b, b) || equal(min_a->a, b))) {
            // min(min(x, y), y) -> min(x, y)
            expr = a;
        } else if (min_b && (equal(min_b->b, a) || equal(min_b->a, a))) {
            // min(y, min(x, y)) -> min(x, y)
            expr = b;
        } else if (min_a && min_a_a && equal(min_a_a->b, b)) {
            // min(min(min(x, y), z), y) -> min(min(x, y), z)
            expr = a;
        } else if (min_a && min_a_a_a && equal(min_a_a_a->b, b)) {
            // min(min(min(min(x, y), z), w), y) -> min(min(min(x, y), z), w)
            expr = a;
        } else if (min_a && min_a_a_a_a && equal(min_a_a_a_a->b, b)) {
            // min(min(min(min(min(x, y), z), w), l), y) -> min(min(min(min(x, y), z), w), l)
            expr = a;
        } else if (max_a && max_b && equal(max_a->a, max_b->a)) {
            // Distributive law for min/max
            // min(max(x, y), max(x, z)) -> max(min(y, z), x)
            expr = mutate(Max::make(Min::make(max_a->b, max_b->b), max_a->a));
        } else if (max_a && max_b && equal(max_a->a, max_b->b)) {
            // min(max(x, y), max(z, x)) -> max(min(y, z), x)
            expr = mutate(Max::make(Min::make(max_a->b, max_b->a), max_a->a));
        } else if (max_a && max_b && equal(max_a->b, max_b->a)) {
            // min(max(y, x), max(x, z)) -> max(min(y, z), x)
            expr = mutate(Max::make(Min::make(max_a->a, max_b->b), max_a->b));
        } else if (max_a && max_b && equal(max_a->b, max_b->b)) {
            // min(max(y, x), max(z, x)) -> max(min(y, z), x)
            expr = mutate(Max::make(Min::make(max_a->a, max_b->a), max_a->b));
        } else if (min_a && min_b && equal(min_a->a, min_b->a)) {
            // min(min(x, y), min(x, z)) -> min(min(y, z), x)
            expr = mutate(Min::make(Min::make(min_a->b, min_b->b), min_a->a));
        } else if (min_a && min_b && equal(min_a->a, min_b->b)) {
            // min(min(x, y), min(z, x)) -> min(min(y, z), x)
            expr = mutate(Min::make(Min::make(min_a->b, min_b->a), min_a->a));
        } else if (min_a && min_b && equal(min_a->b, min_b->a)) {
            // min(min(y, x), min(x, z)) -> min(min(y, z), x)
            expr = mutate(Min::make(Min::make(min_a->a, min_b->b), min_a->b));
        } else if (min_a && min_b && equal(min_a->b, min_b->b)) {
            // min(min(y, x), min(z, x)) -> min(min(y, z), x)
            expr = mutate(Min::make(Min::make(min_a->a, min_b->a), min_a->b));
        } else if (add_a && add_b && no_overflow(op->type) && equal(add_a->b, add_b->b)) {
            // Distributive law for addition
            // min(a + b, c + b) -> min(a, c) + b
            expr = mutate(min(add_a->a, add_b->a)) + add_a->b;
        } else if (add_a && add_b && no_overflow(op->type) && equal(add_a->a, add_b->a)) {
            // min(b + a, b + c) -> min(a, c) + b
            expr = mutate(min(add_a->b, add_b->b)) + add_a->a;
        } else if (add_a && add_b && no_overflow(op->type) && equal(add_a->a, add_b->b)) {
            // min(b + a, c + b) -> min(a, c) + b
            expr = mutate(min(add_a->b, add_b->a)) + add_a->a;
        } else if (add_a && add_b && no_overflow(op->type) && equal(add_a->b, add_b->a)) {
            // min(a + b, b + c) -> min(a, c) + b
            expr = mutate(min(add_a->a, add_b->b)) + add_a->b;
        } else if (min_a && is_simple_const(min_a->b)) {
            if (is_simple_const(b)) {
                // min(min(x, 4), 5) -> min(x, 4)
                expr = Min::make(min_a->a, mutate(Min::make(b, min_a->b)));
            } else {
                // min(min(x, 4), y) -> min(min(x, y), 4)
                expr = mutate(Min::make(Min::make(min_a->a, b), min_a->b));
            }
        } else if (div_a && div_b &&
                   const_int(div_a->b, &ia) && ia &&
                   const_int(div_b->b, &ib) &&
                   (ia == ib)) {
            // min(a / 4, b / 4) -> min(a, b) / 4
            if (ia > 0) {
                expr = mutate(min(div_a->a, div_b->a) / ia);
            } else {
                expr = mutate(max(div_a->a, div_b->a) / ia);
            }
        } else if (mul_a && mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   (ia == ib)) {
            if (ia > 0) {
                expr = mutate(min(mul_a->a, mul_b->a) * ia);
            } else {
                expr = mutate(max(mul_a->a, mul_b->a) * ia);
            }
        } else if (mul_a && const_int(mul_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia && (ib % ia == 0)) {
            // min(x*8, 24) -> min(x, 3)*8
            if (ia > 0) {
                expr = mutate(min(mul_a->a, ib/ia) * ia);
            } else {
                expr = mutate(max(mul_a->a, ib/ia) * ia);
            }
        } else if (call_a && call_a->name == Call::likely && call_a->call_type == Call::Intrinsic &&
                   equal(call_a->args[0], b)) {
            // min(likely(b), b) -> likely(b)
            expr = a;
        } else if (call_b && call_b->name == Call::likely && call_b->call_type == Call::Intrinsic &&
                   equal(call_b->args[0], a)) {
            // min(a, likely(a)) -> likely(a)
            expr = b;
        } else if (no_overflow(op->type) && sub_a && is_const(sub_a->a) && is_const(b)) {
            // min(8 - x, 3) -> 8 - max(x, 5)
            expr = mutate(sub_a->a - max(sub_a->b, sub_a->a - b));
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


        int ia = 0, ib = 0, ic = 0;
        float fa = 0.0f, fb = 0.0f;
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
        const Max *max_a_a = max_a ? max_a->a.as<Max>() : NULL;
        const Max *max_a_a_a = max_a_a ? max_a_a->a.as<Max>() : NULL;
        const Max *max_a_a_a_a = max_a_a_a ? max_a_a_a->a.as<Max>() : NULL;
        const Min *min_a = a.as<Min>();
        const Min *min_b = b.as<Min>();
        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();

        if (equal(a, b)) {
            expr = a;
            return;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            expr = std::max(ia, ib);
            return;
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = std::max(fa, fb);
            return;
        } else if (const_castint(a, &ia) && const_castint(b, &ib)) {
            if (op->type.is_uint()) {
                expr = make_const(op->type, std::max(((unsigned int) ia), ((unsigned int) ib)));
            } else {
                expr = make_const(op->type, std::max(ia, ib));
            }
            return;
        } else if (const_castint(b, &ib) && ib == b.type().imin()) {
            // Compute maximum of expression of type and minimum of type --> expression
            expr = a;
            return;
        } else if (const_castint(b, &ib) && ib == b.type().imax()) {
            // Compute maximum of expression of type and maximum of type --> max of type
            expr = b;
            return;
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(Broadcast::make(Max::make(broadcast_a->value, broadcast_b->value), broadcast_a->width));
            return;
        } else if (op->type == Int(32) && a.as<Variable>() && is_simple_const(b)) {
            Expr delta = mutate(a - b);
            Interval id = bounds_of_expr_in_scope(delta, bounds_info);
            id.min = mutate(id.min);
            id.max = mutate(id.max);
            if (id.min.defined() && (is_zero(id.min) || is_positive_const(id.min))) {
                expr = a;
                return;
            }
            if (id.max.defined() && (is_zero(id.max) || is_negative_const(id.max))) {
                expr = b;
                return;
            }
        } else if (ramp_a && broadcast_b &&
                   const_int(ramp_a->base, &ia) &&
                   const_int(ramp_a->stride, &ib) &&
                   const_int(broadcast_b->value, &ic)) {
            // max(ramp(a, b, n), broadcast(c, n))
            int ramp_start = ia;
            int ramp_end = ia + ib * (ramp_a->width - 1);
            if (ramp_start >= ic && ramp_end >= ic) {
                // ramp dominates
                expr = a;
                return;
            } if (ramp_start <= ic && ramp_end <= ic) {
                // broadcast dominates
                expr = b;
                return;
            }
        }

        if (add_a && const_int(add_a->b, &ia) && add_b && const_int(add_b->b, &ib) && equal(add_a->a, add_b->a)) {
            // max(x + 3, x - 2) -> x - 2
            if (ia > ib) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (add_a && const_int(add_a->b, &ia) && equal(add_a->a, b)) {
            // max(x + 5, x)
            if (ia > 0) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (add_b && const_int(add_b->b, &ib) && equal(add_b->a, a)) {
            // max(x, x + 5)
            if (ib > 0) {
                expr = b;
            } else {
                expr = a;
            }
        } else if (sub_a && sub_b && equal(sub_a->b, sub_b->b) &&
                   const_int(sub_a->a, &ia) && const_int(sub_b->a, &ib)) {
            // max (100-x, 101-x) -> 101-x
            if (ia > ib) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (min_a && equal(min_a->b, b)) {
            // max(min(x, y), y) -> y
            expr = b;
        } else if (max_a && (equal(max_a->b, b) || equal(max_a->a, b))) {
            // max(max(x, y), y) -> max(x, y)
            expr = a;
        } else if (max_b && (equal(max_b->b, a) || equal(max_b->a, a))) {
            // max(y, max(x, y)) -> max(x, y)
            expr = b;
        } else if (max_a_a && equal(max_a_a->b, b)) {
            // max(max(max(x, y), z), y) -> max(max(x, y), z)
            expr = a;
        } else if (max_a_a_a && equal(max_a_a_a->b, b)) {
            // max(max(max(max(x, y), z), w), y) -> max(max(max(x, y), z), w)
            expr = a;
        } else if (max_a_a_a_a && equal(max_a_a_a_a->b, b)) {
            // max(max(max(max(max(x, y), z), w), l), y) -> max(max(max(max(x, y), z), w), l)
            expr = a;

        } else if (max_a && max_b && equal(max_a->a, max_b->a)) {
            // Distributive law for min/max
            // max(max(x, y), max(x, z)) -> max(max(y, z), x)
            expr = mutate(Max::make(Max::make(max_a->b, max_b->b), max_a->a));
        } else if (max_a && max_b && equal(max_a->a, max_b->b)) {
            // max(max(x, y), max(z, x)) -> max(max(y, z), x)
            expr = mutate(Max::make(Max::make(max_a->b, max_b->a), max_a->a));
        } else if (max_a && max_b && equal(max_a->b, max_b->a)) {
            // max(max(y, x), max(x, z)) -> max(max(y, z), x)
            expr = mutate(Max::make(Max::make(max_a->a, max_b->b), max_a->b));
        } else if (max_a && max_b && equal(max_a->b, max_b->b)) {
            // max(max(y, x), max(z, x)) -> max(max(y, z), x)
            expr = mutate(Max::make(Max::make(max_a->a, max_b->a), max_a->b));
        } else if (min_a && min_b && equal(min_a->a, min_b->a)) {
            // max(min(x, y), min(x, z)) -> min(max(y, z), x)
            expr = mutate(Min::make(Max::make(min_a->b, min_b->b), min_a->a));
        } else if (min_a && min_b && equal(min_a->a, min_b->b)) {
            // max(min(x, y), min(z, x)) -> min(max(y, z), x)
            expr = mutate(Min::make(Max::make(min_a->b, min_b->a), min_a->a));
        } else if (min_a && min_b && equal(min_a->b, min_b->a)) {
            // max(min(y, x), min(x, z)) -> min(max(y, z), x)
            expr = mutate(Min::make(Max::make(min_a->a, min_b->b), min_a->b));
        } else if (min_a && min_b && equal(min_a->b, min_b->b)) {
            // max(min(y, x), min(z, x)) -> min(max(y, z), x)
            expr = mutate(Min::make(Max::make(min_a->a, min_b->a), min_a->b));
        } else if (add_a && add_b && no_overflow(op->type) && equal(add_a->b, add_b->b)) {
            // Distributive law for addition
            // max(a + b, c + b) -> max(a, c) + b
            expr = mutate(max(add_a->a, add_b->a)) + add_a->b;
        } else if (add_a && add_b && no_overflow(op->type) && equal(add_a->a, add_b->a)) {
            // max(b + a, b + c) -> max(a, c) + b
            expr = mutate(max(add_a->b, add_b->b)) + add_a->a;
        } else if (add_a && add_b && no_overflow(op->type) && equal(add_a->a, add_b->b)) {
            // max(b + a, c + b) -> max(a, c) + b
            expr = mutate(max(add_a->b, add_b->a)) + add_a->a;
        } else if (add_a && add_b && no_overflow(op->type) && equal(add_a->b, add_b->a)) {
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
        } else if (div_a && div_b &&
                   const_int(div_a->b, &ia) && ia &&
                   const_int(div_b->b, &ib) &&
                   (ia == ib)) {
            // max(a / 4, b / 4) -> max(a, b) / 4
            if (ia > 0) {
                expr = mutate(max(div_a->a, div_b->a) / ia);
            } else {
                expr = mutate(min(div_a->a, div_b->a) / ia);
            }
        } else if (mul_a && mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   (ia == ib)) {
            if (ia > 0) {
                expr = mutate(max(mul_a->a, mul_b->a) * ia);
            } else {
                expr = mutate(min(mul_a->a, mul_b->a) * ia);
            }
        } else if (mul_a && const_int(mul_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia && (ib % ia == 0)) {
            // max(x*8, 24) -> max(x, 3)*8
            if (ia > 0) {
                expr = mutate(max(mul_a->a, ib/ia) * ia);
            } else {
                expr = mutate(min(mul_a->a, ib/ia) * ia);
            }
        } else if (call_a && call_a->name == Call::likely && call_a->call_type == Call::Intrinsic &&
                   equal(call_a->args[0], b)) {
            // max(likely(b), b) -> likely(b)
            expr = a;
        } else if (call_b && call_b->name == Call::likely && call_b->call_type == Call::Intrinsic &&
                   equal(call_b->args[0], a)) {
            // max(a, likely(a)) -> likely(a)
            expr = b;
        } else if (no_overflow(op->type) && sub_a && is_const(sub_a->a) && is_const(b)) {
            // max(8 - x, 3) -> 8 - min(x, 5)
            expr = mutate(sub_a->a - min(sub_a->b, sub_a->a - b));
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

        Expr zero = make_zero(delta.type());

        if (is_zero(delta)) {
            expr = const_true(op->type.width);
            return;
        } else if (is_const(delta)) {
            bool t = true;
            bool f = true;
            for (int i = 0; i < delta.type().width; i++) {
                Expr deltai = extract_lane(delta, i);
                if (is_zero(deltai)) {
                    f = false;
                } else {
                    t = false;
                }
            }
            if (t) {
                expr = const_true(op->type.width);
                return;
            } else if (f) {
                expr = const_false(op->type.width);
                return;
            }
        } else if (delta.type() == Int(32)) {
            // Attempt to disprove using modulus remainder analysis
            ModulusRemainder mod_rem = modulus_remainder(delta, alignment_info);
            if (mod_rem.remainder) {
                expr = const_false();
                return;
            }

            // Attempt to disprove using bounds analysis
            Interval i = bounds_of_expr_in_scope(delta, bounds_info);
            if (i.min.defined() && i.max.defined()) {
                i.min = mutate(i.min);
                i.max = mutate(i.max);
                if (is_positive_const(i.min)) {
                    expr = const_false();
                    return;
                }
                if (is_negative_const(i.max)) {
                    expr = const_false();
                    return;
                }
            }
        }

        if (broadcast) {
            // Push broadcasts outwards
            expr = Broadcast::make(mutate(broadcast->value ==
                                          make_zero(broadcast->value.type())),
                                   broadcast->width);
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
        } else if (mul && mul->type == Int(32)) {
            // Restrict to int32, because, e.g. 64 * 4 == 0 as a uint8.
            expr = mutate(mul->a == zero || mul->b == zero);
        } else {
            expr = (delta == make_zero(delta.type()));
        }
    }

    void visit(const NE *op) {
        expr = mutate(Not::make(op->a == op->b));
    }

    void visit(const LT *op) {
        Expr a = mutate(op->a), b = mutate(op->b);
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
        const Min *min_a = a.as<Min>();
        const Min *min_b = b.as<Min>();
        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();
        const Div *div_a_a = mul_a ? mul_a->a.as<Div>() : NULL;
        const Mul *delta_ramp_mul = delta_ramp ? delta_ramp->base.as<Mul>() : NULL;

        int ia = 0, ib = 0, ic = 0;

        if (delta.type() == Int(32) && !is_const(delta)) {
            Interval i = bounds_of_expr_in_scope(delta, bounds_info);
            i.max = mutate(i.max);
            i.min = mutate(i.min);
            if (i.max.defined() && is_negative_const(i.max)) {
                expr = const_true();
                return;
            }
            if (i.min.defined() && (is_zero(i.min) || is_positive_const(i.min))) {
                expr = const_false();
                return;
            }
        }

        // Note that the computation of delta could be incorrect if
        // ia and/or ib are large unsigned integer constants, especially when
        // int is 32 bits on the machine.
        // Explicit comparison is preferred.
        if (const_castint(a, &ia) && const_castint(b, &ib)) {
            if (a.type().is_uint()) {
                expr = make_bool(((unsigned int) ia) < ((unsigned int) ib), op->type.width);
            } else {
                expr = make_bool(ia < ib, op->type.width);
            }
        } else if (const_castint(a, &ia) && ia == a.type().imax()) {
            // Comparing maximum of type < expression of type.  This can never be true.
            expr = const_false(op->type.width);
        } else if (const_castint(b, &ib) && ib == b.type().imin()) {
            // Comparing expression of type < minimum of type.  This can never be true.
            expr = const_false(op->type.width);
        } else if (is_zero(delta) || (no_overflow(delta.type()) && is_positive_const(delta))) {
            expr = const_false(op->type.width);
        } else if (no_overflow(delta.type()) && is_negative_const(delta)) {
            expr = const_true(op->type.width);
        } else if (broadcast_a && broadcast_b) {
            // Push broadcasts outwards
            expr = mutate(Broadcast::make(broadcast_a->value < broadcast_b->value, broadcast_a->width));
        } else if (no_overflow(delta.type())) {
            if (ramp_a && ramp_b && equal(ramp_a->stride, ramp_b->stride)) {
                // Ramps with matching stride
                Expr bases_lt = (ramp_a->base < ramp_b->base);
                expr = mutate(Broadcast::make(bases_lt, ramp_a->width));
            } else if (add_a && add_b && equal(add_a->a, add_b->a)) {
                // Subtract a term from both sides
                expr = mutate(add_a->b < add_b->b);
            } else if (add_a && add_b && equal(add_a->a, add_b->b)) {
                expr = mutate(add_a->b < add_b->a);
            } else if (add_a && add_b && equal(add_a->b, add_b->a)) {
                expr = mutate(add_a->a < add_b->b);
            } else if (add_a && add_b && equal(add_a->b, add_b->b)) {
                expr = mutate(add_a->a < add_b->a);
            } else if (sub_a && sub_b && equal(sub_a->a, sub_b->a)) {
                // Add a term to both sides and negate.
                expr = mutate(sub_b->b < sub_a->b);
            } else if (sub_a && sub_b && equal(sub_a->b, sub_b->b)) {
                expr = mutate(sub_a->a < sub_b->a);
            } else if (add_a) {
                // Rearrange so that all adds and subs are on the rhs to cut down on further cases
                expr = mutate(add_a->a < (b - add_a->b));
            } else if (sub_a) {
                expr = mutate(sub_a->a < (b + sub_a->b));
            } else if (add_b && equal(add_b->a, a)) {
                // Subtract a term from both sides
                expr = mutate(make_zero(add_b->b.type()) < add_b->b);
            } else if (add_b && equal(add_b->b, a)) {
                expr = mutate(make_zero(add_b->a.type()) < add_b->a);
            } else if (add_b && const_int(a, &ia) && const_int(add_b->b, &ib)) {
                // ia < x + ib
                expr = mutate((ia - ib) < add_b->a);
            } else if (sub_b && equal(sub_b->a, a)) {
                // Add a term to both sides
                expr = mutate(sub_b->b < make_zero(sub_b->b.type()));
            } else if (mul_a && mul_b &&
                       is_positive_const(mul_a->b) && is_positive_const(mul_b->b) &&
                       equal(mul_a->b, mul_b->b)) {
                // Divide both sides by a constant
                expr = mutate(mul_a->a < mul_b->a);
            } else if (min_a) {
                Expr lt_a = mutate(min_a->a < b);
                Expr lt_b = mutate(min_a->b < b);
                if (is_one(lt_a) || is_one(lt_b)) {
                    expr = const_true();
                } else if (is_zero(lt_a) && is_zero(lt_b)) {
                    expr = const_false();
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    expr = op;
                } else {
                    expr = LT::make(a, b);
                }
            } else if (max_a) {
                Expr lt_a = mutate(max_a->a < b);
                Expr lt_b = mutate(max_a->b < b);
                if (is_one(lt_a) && is_one(lt_b)) {
                    expr = const_true();
                } else if (is_zero(lt_a) || is_zero(lt_b)) {
                    expr = const_false();
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    expr = op;
                } else {
                    expr = LT::make(a, b);
                }
            } else if (min_b) {
                Expr lt_a = mutate(a < min_b->a);
                Expr lt_b = mutate(a < min_b->b);
                if (is_one(lt_a) && is_one(lt_b)) {
                    expr = const_true();
                } else if (is_zero(lt_a) || is_zero(lt_b)) {
                    expr = const_false();
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    expr = op;
                } else {
                    expr = LT::make(a, b);
                }
            } else if (max_b) {
                Expr lt_a = mutate(a < max_b->a);
                Expr lt_b = mutate(a < max_b->b);
                if (is_one(lt_a) || is_one(lt_b)) {
                    expr = const_true();
                } else if (is_zero(lt_a) && is_zero(lt_b)) {
                    expr = const_false();
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    expr = op;
                } else {
                    expr = LT::make(a, b);
                }
            } else if (mul_a && div_a_a && add_b &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       const_int(add_b->b, &ic) &&
                       ia > 0 &&
                       ia == ib && ia <= -ic &&
                       equal(div_a_a->a, add_b->a)) {
                // (x/c1)*c1 < x + c2 where c1 <= -c2 -> false
                expr = const_false();
            } else if (delta_ramp && is_positive_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base + delta_ramp->stride*(delta_ramp->width - 1) < 0))) {
                expr = const_true(delta_ramp->width);
            } else if (delta_ramp && is_positive_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base >= 0))) {
                expr = const_false(delta_ramp->width);
            } else if (delta_ramp && is_negative_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base < 0))) {
                expr = const_true(delta_ramp->width);
            } else if (delta_ramp && is_negative_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base + delta_ramp->stride*(delta_ramp->width - 1) >= 0))) {
                expr = const_false(delta_ramp->width);
            } else if (delta_ramp_mul && const_int(delta_ramp_mul->b, &ia) &&
                       is_one(mutate(abs(ia) >= abs(delta_ramp->stride*delta_ramp->width)))) {
                // Ramp(x*a, b, c) < 0 -> Broadcast(x, c) if |a| >= |b*c|
                expr = Broadcast::make(mutate(LT::make(delta_ramp_mul->a, 0)), delta_ramp->width);
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
        } else if (le_a && le_b && equal(le_a->a, le_b->a)) {
            // (x <= foo && x <= bar) -> x <= min(foo, bar)
            expr = mutate(le_a->a <= min(le_a->b, le_b->b));
        } else if (le_a && le_b && equal(le_a->b, le_b->b)) {
            // (foo <= x && bar <= x) -> max(foo, bar) <= x
            expr = mutate(max(le_a->a, le_b->a) <= le_a->b);
        } else if (lt_a && lt_b && equal(lt_a->a, lt_b->a)) {
            // (x < foo && x < bar) -> x < min(foo, bar)
            expr = mutate(lt_a->a < min(lt_a->b, lt_b->b));
        } else if (lt_a && lt_b && equal(lt_a->b, lt_b->b)) {
            // (foo < x && bar < x) -> max(foo, bar) < x
            expr = mutate(max(lt_a->a, lt_b->a) < lt_a->b);
        } else if (eq_a && neq_b &&
                   ((equal(eq_a->a, neq_b->a) && equal(eq_a->b, neq_b->b)) ||
                    (equal(eq_a->a, neq_b->b) && equal(eq_a->b, neq_b->a)))) {
            // a == b && a != b
            expr = const_false(op->type.width);
        } else if (eq_b && neq_a &&
                   ((equal(eq_b->a, neq_a->a) && equal(eq_b->b, neq_a->b)) ||
                    (equal(eq_b->a, neq_a->b) && equal(eq_b->b, neq_a->a)))) {
            // a != b && a == b
            expr = const_false(op->type.width);
        } else if ((not_a && equal(not_a->a, b)) ||
                   (not_b && equal(not_b->a, a))) {
            // a && !a
            expr = const_false(op->type.width);
        } else if (le_a && lt_b &&
                   equal(le_a->a, lt_b->b) &&
                   equal(le_a->b, lt_b->a)) {
            // a <= b && b < a
            expr = const_false(op->type.width);
        } else if (lt_a && le_b &&
                   equal(lt_a->a, le_b->b) &&
                   equal(lt_a->b, le_b->a)) {
            // a < b && b <= a
            expr = const_false(op->type.width);
        } else if (broadcast_a && broadcast_b &&
                   broadcast_a->width == broadcast_b->width) {
            // x8(a) && x8(b) -> x8(a && b)
            expr = Broadcast::make(mutate(And::make(broadcast_a->value, broadcast_b->value)), broadcast_a->width);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
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
        } else if (eq_a && neq_b &&
                   ((equal(eq_a->a, neq_b->a) && equal(eq_a->b, neq_b->b)) ||
                    (equal(eq_a->a, neq_b->b) && equal(eq_a->b, neq_b->a)))) {
            // a == b || a != b
            expr = const_true(op->type.width);
        } else if (neq_a && eq_b &&
                   ((equal(eq_b->a, neq_a->a) && equal(eq_b->b, neq_a->b)) ||
                    (equal(eq_b->a, neq_a->b) && equal(eq_b->b, neq_a->a)))) {
            // a != b || a == b
            expr = const_true(op->type.width);
        } else if ((not_a && equal(not_a->a, b)) ||
                   (not_b && equal(not_b->a, a))) {
            // a || !a
            expr = const_true(op->type.width);
        } else if (le_a && lt_b &&
                   equal(le_a->a, lt_b->b) &&
                   equal(le_a->b, lt_b->a)) {
            // a <= b || b < a
            expr = const_true(op->type.width);
        } else if (lt_a && le_b &&
                   equal(lt_a->a, le_b->b) &&
                   equal(lt_a->b, le_b->a)) {
            // a < b || b <= a
            expr = const_true(op->type.width);
        } else if (broadcast_a && broadcast_b &&
                   broadcast_a->width == broadcast_b->width) {
            // x8(a) || x8(b) -> x8(a || b)
            expr = Broadcast::make(mutate(Or::make(broadcast_a->value, broadcast_b->value)), broadcast_a->width);
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
            expr = mutate(Broadcast::make(!n->value, n->width));
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

        if (is_zero(condition)) {
            expr = false_value;
        } else if (is_one(condition)) {
            expr = true_value;
        } else if (equal(true_value, false_value)) {
            expr = true_value;
        } else if (const Broadcast *b = condition.as<Broadcast>()) {
            // Select of broadcast -> scalar select
            expr = mutate(Select::make(b->value, true_value, false_value));
        } else if (const NE *ne = condition.as<NE>()) {
            // Normalize select(a != b, c, d) to select(a == b, d, c)
            expr = mutate(Select::make(ne->a == ne->b, false_value, true_value));
        } else if (const LE *le = condition.as<LE>()) {
            // Normalize select(a <= b, c, d) to select(b < a, d, c)
            expr = mutate(Select::make(le->b < le->a, false_value, true_value));
        } else if (ct && ct->name == Call::likely && ct->call_type == Call::Intrinsic &&
                   equal(ct->args[0], false_value)) {
            // select(cond, likely(a), a) -> likely(a)
            expr = true_value;
        } else if (cf && cf->name == Call::likely && cf->call_type == Call::Intrinsic &&
                   equal(cf->args[0], true_value)) {
            // select(cond, a, likely(a)) -> likely(a)
            expr = false_value;
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
            expr = Broadcast::make(base, op->width);
        } else if (base.same_as(op->base) &&
                   stride.same_as(op->stride)) {
            expr = op;
        } else {
            expr = Ramp::make(base, stride, op->width);
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
        std::vector<Expr> stack;
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
            expr = Broadcast::make(load, b->width);
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

        if (op->call_type == Call::Intrinsic &&
            (op->name == Call::shift_left ||
             op->name == Call::shift_right)) {
            Expr a = mutate(op->args[0]), b = mutate(op->args[1]);
            int ib = 0;

            if (const_castint(b, &ib)) {
                Type t = op->type;

                bool shift_left = op->name == Call::shift_left;
                if (ib < 0) {
                    shift_left = !shift_left;
                    ib = -ib;
                }

                if (ib < std::min(t.bits, 32)) {
                    ib = 1 << ib;
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
            } else if (op->name == Call::shift_left) {
                expr = a << b;
            } else {
                expr = a >> b;
            }
        } else if (op->call_type == Call::Intrinsic && op->name == Call::bitwise_and) {
            Expr a = mutate(op->args[0]), b = mutate(op->args[1]);
            int ib = 0;
            int bits;

            if (const_castint(b, &ib) &&
                ((ib < b.type().imax()) && (ib < std::numeric_limits<int>::max()) &&
                 is_const_power_of_two_integer(ib + 1, &bits))) {
                  expr = Mod::make(a, ib + 1);
            } else  if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
                expr = op;
            } else {
                expr = a & b;
            }
        } else if (op->call_type == Call::Intrinsic &&
                   op->name == Call::abs) {
            // Constant evaluate abs(x).
            Expr a = mutate(op->args[0]);
            Type ta = a.type();
            int ia = 0;
            if (ta.is_int() && const_castint(a, &ia) && ia != ta.imin()) {
                if (ia < 0) {
                    ia = -ia;
                }
                expr = Cast::make(op->type, ia);
            } else if (ta.is_uint()) {
                // abs(uint) is a no-op.
                expr = a;
            } else if (a.same_as(op->args[0])) {
                expr = op;
            } else {
                expr = abs(a);
            }
        } else if (op->call_type == Call::Extern &&
                   op->name == "is_nan_f32") {
            Expr arg = mutate(op->args[0]);
            float f = 0.0f;
            if (const_float(arg, &f)) {
                expr = std::isnan(f);
            } else if (arg.same_as(op->args[0])) {
                expr = op;
            } else {
                expr = Call::make(op->type, op->name, {arg}, op->call_type);
            }
        } else if (op->call_type == Call::Intrinsic &&
                   op->name == Call::stringify) {
            // Eagerly concat constant arguments to a stringify.
            bool changed = false;
            std::vector<Expr> new_args;
            const StringImm *last = NULL;
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
                    snprintf(buf, sizeof(buf), "%d", int_imm->value);
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
        } else if (op->call_type == Call::Extern &&
                   op->name == "log_f32") {
            Expr arg = mutate(op->args[0]);
            if (const float *f = as_const_float(arg)) {
                expr = logf(*f);
            } else if (!arg.same_as(op->args[0])) {
                expr = Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                expr = op;
            }
        } else if (op->call_type == Call::Extern &&
                   op->name == "exp_f32") {
            Expr arg = mutate(op->args[0]);
            if (const float *f = as_const_float(arg)) {
                expr = expf(*f);
            } else if (!arg.same_as(op->args[0])) {
                expr = Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                expr = op;
            }
        } else if (op->call_type == Call::Extern &&
                   (op->name == "floor_f32" || op->name == "ceil_f32" ||
                    op->name == "round_f32" || op->name == "trunc_f32")) {
            internal_assert(op->args.size() == 1);
            Expr arg = mutate(op->args[0]);
            const Call *call = arg.as<Call>();
            if (const float *f = as_const_float(arg)) {
                if (op->name == "floor_f32") expr = std::floor(*f);
                else if (op->name == "ceil_f32") expr = std::ceil(*f);
                else if (op->name == "round_f32") expr = std::nearbyint(*f);
                else if (op->name == "trunc_f32") {
                    expr = (*f < 0 ? std::ceil(*f) : std::floor(*f));
                }

            } else if (call && call->call_type == Call::Extern &&
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
            const Broadcast *broadcast = new_value.as<Broadcast>();

            const Variable *var_b = NULL;
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
            } else if (ramp && is_const(ramp->stride)) {
                new_var = Variable::make(new_value.type().element_of(), new_name);
                replacement = substitute(new_name, Ramp::make(new_var, ramp->stride, ramp->width), replacement);
                new_value = ramp->base;
            } else if (broadcast) {
                new_var = Variable::make(new_value.type().element_of(), new_name);
                replacement = substitute(new_name, Broadcast::make(new_var, broadcast->width), replacement);
                new_value = broadcast->value;
            } else if (cast && cast->type.bits > cast->value.type().bits) {
                // Widening casts get pushed inwards, narrowing casts
                // stay outside. This keeps the temporaries small, and
                // helps with peephole optimizations in codegen that
                // skip the widening entirely.
                new_var = Variable::make(cast->value.type(), new_name);
                replacement = substitute(new_name, Cast::make(cast->type, new_var), replacement);
                new_value = cast->value;
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
        bool new_value_tracked = false;
        if (new_value.defined() && new_value.type() == Int(32)) {
            ModulusRemainder mod_rem = modulus_remainder(new_value, alignment_info);
            if (mod_rem.modulus > 1) {
                alignment_info.push(new_name, mod_rem);
                new_value_tracked = true;
            }
        }
        bool value_tracked = false;
        if (value.type() == Int(32)) {
            ModulusRemainder mod_rem = modulus_remainder(value, alignment_info);
            if (mod_rem.modulus > 1) {
                alignment_info.push(op->name, mod_rem);
                value_tracked = true;
            }
        }

        body = mutate(body);

        if (value_tracked) {
            alignment_info.pop(op->name);
        }
        if (new_value_tracked) {
            alignment_info.pop(new_name);
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
            user_error << "This pipeline is guaranteed to fail an assertion at runtime: \n"
                       << stmt << "\n";
        } else if (a && is_one(a->condition)) {
            stmt = Evaluate::make(0);
        }
    }


    void visit(const For *op) {
        Expr new_min = mutate(op->min);
        Expr new_extent = mutate(op->extent);

        const IntImm *new_min_int = new_min.as<IntImm>();
        const IntImm *new_extent_int = new_extent.as<IntImm>();
        bool bounds_tracked = new_min_int && new_extent_int;
        if (bounds_tracked) {
            Interval i = Interval(new_min, new_min_int->value + new_extent_int->value - 1);
            bounds_info.push(op->name, i);
        }

        Stmt new_body = mutate(op->body);

        if (bounds_tracked) {
            bounds_info.pop(op->name);
        }

        if (is_no_op(new_body)) {
            stmt = new_body;
            return;
        }

        if (op->min.same_as(new_min) &&
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
            stmt = Store::make(op->name, value, index);
        }
    }

    void visit(const Block *op) {
        Stmt first = mutate(op->first);

        if (!op->rest.defined()) {
            stmt = first;
        } else {
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
            } else if (let_first && let_rest &&
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
            } else if (if_first && if_rest && equal(if_first->condition, if_rest->condition)) {
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
            } else if (op->first.same_as(first) && op->rest.same_as(rest)) {
                stmt = op;
            } else {
                stmt = Block::make(first, rest);
            }

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

template <typename T>
void test_int_cast_constant() {
    Type t = type_of<T>();

    int min = t.imin();
    internal_assert(int_cast_constant(t, min - 1) == (int)((T)(min - 1)))
        << "Simplify test failed: int_cast_constant\n";
    internal_assert(int_cast_constant(t, min) == (int)((T)min))
        << "Simplify test failed: int_cast_constant\n";
    internal_assert(int_cast_constant(t, min + 1) == (int)((T)(min + 1)))
        << "Simplify test failed: int_cast_constant\n";

    int max = t.imax();
    internal_assert(int_cast_constant(t, max - 1) == (int)((T)(max - 1)))
        << "Simplify test failed: int_cast_constant\n";
    internal_assert(int_cast_constant(t, max) == (int)((T)max))
        << "Simplify test failed: int_cast_constant\n";
    internal_assert(int_cast_constant(t, max + 1) == (int)((T)(max + 1)))
        << "Simplify test failed: int_cast_constant\n";

    internal_assert(int_cast_constant(t, -1) == (int)((T)(-1)))
        << "Simplify test failed: int_cast_constant\n";
    internal_assert(int_cast_constant(t, 0) == (int)((T)0))
        << "Simplify test failed: int_cast_constant\n";
    internal_assert(int_cast_constant(t, 1) == (int)((T)1))
        << "Simplify test failed: int_cast_constant\n";

    // Test some random integers.
    for (int i = 0; i < 100; i++) {
        int x = rand() - RAND_MAX/2;
        internal_assert(int_cast_constant(t, x) == (int)((T)x))
            << "Simplify test failed: int_cast_constant\n";
    }
}

}

void simplify_test() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w"), v = Var("v");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);
    Expr t = const_true(), f = const_false();

    // Check the type casting operations.
    test_int_cast_constant<int8_t>();
    test_int_cast_constant<int16_t>();
    test_int_cast_constant<int32_t>();
    test_int_cast_constant<uint8_t>();
    test_int_cast_constant<uint16_t>();
    test_int_cast_constant<uint32_t>();

    check(Cast::make(Int(32), Cast::make(Int(32), x)), x);
    check(Cast::make(Float(32), 3), 3.0f);
    check(Cast::make(Int(32), 5.0f), 5);

    check(Cast::make(Int(32), Cast::make(Int(8), 3)), 3);
    check(Cast::make(Int(32), Cast::make(Int(8), 1232)), -48);

    // Check evaluation of constant expressions involving casts
    check(cast(UInt(16), 53) + cast(UInt(16), 87), cast(UInt(16), 140));
    check(cast(Int(8), 127) + cast(Int(8), 1), cast(Int(8), -128));
    check(cast(UInt(16), -1) - cast(UInt(16), 1), cast(UInt(16), 65534));
    check(cast(Int(16), 4) * cast(Int(16), -5), cast(Int(16), -20));
    check(cast(Int(16), 16) / cast(Int(16), 4), cast(Int(16), 4));
    check(cast(Int(16), 23) % cast(Int(16), 5), cast(Int(16), 3));
    check(min(cast(Int(16), 30000), cast(Int(16), -123)), cast(Int(16), -123));
    check(max(cast(Int(16), 30000), cast(Int(16), 65000)), cast(Int(16), 30000));
    check(cast(UInt(16), -1) == cast(UInt(16), 65535), const_true());
    check(cast(UInt(16), 65) == cast(UInt(16), 66), const_false());
    check(cast(UInt(16), -1) < cast(UInt(16), 65535), const_false());
    check(cast(UInt(16), 65) < cast(UInt(16), 66), const_true());
    // Specific checks for 32 bit unsigned expressions - ensure simplifications are actually unsigned.
    // 4000000000 (4 billion) is less than 2^32 but more than 2^31.  As an int, it is negative.
    check(cast(UInt(32), (int) 4000000000UL) + cast(UInt(32), 5), cast(UInt(32), (int) 4000000005UL));
    check(cast(UInt(32), (int) 4000000000UL) - cast(UInt(32), 5), cast(UInt(32), (int) 3999999995UL));
    check(cast(UInt(32), (int) 4000000000UL) / cast(UInt(32), 5), cast(UInt(32), 800000000));
    check(cast(UInt(32), 800000000) * cast(UInt(32), 5), cast(UInt(32), (int) 4000000000UL));
    check(cast(UInt(32), (int) 4000000023UL) % cast(UInt(32), 100), cast(UInt(32), 23));
    check(min(cast(UInt(32), (int) 4000000023UL) , cast(UInt(32), 1000)), cast(UInt(32), (int) 1000));
    check(max(cast(UInt(32), (int) 4000000023UL) , cast(UInt(32), 1000)), cast(UInt(32), (int) 4000000023UL));
    check(cast(UInt(32), (int) 4000000023UL) < cast(UInt(32), 1000), const_false());
    check(cast(UInt(32), (int) 4000000023UL) == cast(UInt(32), 1000), const_false());

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

    check(3 + x, x + 3);
    check(Expr(3) + Expr(8), 11);
    check(Expr(3.25f) + Expr(7.75f), 11.0f);
    check(x + 0, x);
    check(0 + x, x);
    check(Expr(Ramp::make(x, 2, 3)) + Expr(Ramp::make(y, 4, 3)), Ramp::make(x+y, 6, 3));
    check(Expr(Broadcast::make(4.0f, 5)) + Expr(Ramp::make(3.25f, 4.5f, 5)), Ramp::make(7.25f, 4.5f, 5));
    check(Expr(Ramp::make(3.25f, 4.5f, 5)) + Expr(Broadcast::make(4.0f, 5)), Ramp::make(7.25f, 4.5f, 5));
    check(Expr(Broadcast::make(3, 3)) + Expr(Broadcast::make(1, 3)), Broadcast::make(4, 3));
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
    check(Expr(Ramp::make(x, 2, 3)) - Expr(Ramp::make(y, 4, 3)), Ramp::make(x-y, -2, 3));
    check(Expr(Broadcast::make(4.0f, 5)) - Expr(Ramp::make(3.25f, 4.5f, 5)), Ramp::make(0.75f, -4.5f, 5));
    check(Expr(Ramp::make(3.25f, 4.5f, 5)) - Expr(Broadcast::make(4.0f, 5)), Ramp::make(-0.75f, 4.5f, 5));
    check(Expr(Broadcast::make(3, 3)) - Expr(Broadcast::make(1, 3)), Broadcast::make(2, 3));
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

    check(x*0, 0);
    check(0*x, 0);
    check(x*1, x);
    check(1*x, x);
    check(Expr(2.0f)*4.0f, 8.0f);
    check(Expr(2)*4, 8);
    check((3*x)*4, x*12);
    check(4*(3+x), x*4 + 12);
    check(Expr(Broadcast::make(4.0f, 5)) * Expr(Ramp::make(3.0f, 4.0f, 5)), Ramp::make(12.0f, 16.0f, 5));
    check(Expr(Ramp::make(3.0f, 4.0f, 5)) * Expr(Broadcast::make(2.0f, 5)), Ramp::make(6.0f, 8.0f, 5));
    check(Expr(Broadcast::make(3, 3)) * Expr(Broadcast::make(2, 3)), Broadcast::make(6, 3));

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
    check((x - y)*-2, (y - x)*2);
    check((xf - yf)*-2.0f, (yf - xf)*2.0f);

    check(xf / 4.0f, xf * 0.25f);
    check(Expr(Broadcast::make(y, 4)) / Expr(Broadcast::make(x, 4)),
          Expr(Broadcast::make(y/x, 4)));
    check(Expr(Ramp::make(x, 4, 4)) / 2, Ramp::make(x/2, 2, 4));
    check(Expr(Ramp::make(x, -4, 7)) / 2, Ramp::make(x/2, -2, 7));
    check(Expr(Ramp::make(x, 4, 5)) / -2, Ramp::make(x/-2, -2, 5));
    check(Expr(Ramp::make(x, -8, 5)) / -2, Ramp::make(x/-2, 4, 5));

    check(Expr(Ramp::make(4*x, 1, 4)) / 4, Broadcast::make(x, 4));
    check(Expr(Ramp::make(x*4, 1, 3)) / 4, Broadcast::make(x, 3));
    check(Expr(Ramp::make(x*8, 2, 4)) / 8, Broadcast::make(x, 4));
    check(Expr(Ramp::make(x*8, 3, 3)) / 8, Broadcast::make(x, 3));
    check(Expr(Ramp::make(0, 1, 8)) % 16, Expr(Ramp::make(0, 1, 8)));
    check(Expr(Ramp::make(8, 1, 8)) % 16, Expr(Ramp::make(8, 1, 8)));
    check(Expr(Ramp::make(9, 1, 8)) % 16, Expr(Ramp::make(9, 1, 8)) % 16);
    check(Expr(Ramp::make(16, 1, 8)) % 16, Expr(Ramp::make(0, 1, 8)));
    check(Expr(Ramp::make(0, 1, 8)) % 8, Expr(Ramp::make(0, 1, 8)));
    check(Expr(Ramp::make(x*8+17, 1, 4)) % 8, Expr(Ramp::make(1, 1, 4)));
    check(Expr(Ramp::make(x*8+17, 1, 8)) % 8, Expr(Ramp::make(1, 1, 8) % 8));


    check(Expr(7) % 2, 1);
    check(Expr(7.25f) % 2.0f, 1.25f);
    check(Expr(-7.25f) % 2.0f, 0.75f);
    check(Expr(-7.25f) % -2.0f, -1.25f);
    check(Expr(7.25f) % -2.0f, -0.75f);
    check(Expr(Broadcast::make(x, 4)) % Expr(Broadcast::make(y, 4)),
          Expr(Broadcast::make(x % y, 4)));
    check((x*8) % 4, 0);
    check((x*8 + y) % 4, y % 4);
    check((y + 8) % 4, y % 4);
    check((y + x*8) % 4, y % 4);
    check((y*16 + 13) % 2, 1);
    check(Expr(Ramp::make(x, 2, 4)) % (Broadcast::make(2, 4)),
          Broadcast::make(x % 2, 4));
    check(Expr(Ramp::make(2*x+1, 4, 4)) % (Broadcast::make(2, 4)),
          Broadcast::make(1, 4));

    check(Min::make(7, 3), 3);
    check(Min::make(4.25f, 1.25f), 1.25f);
    check(Min::make(Broadcast::make(x, 4), Broadcast::make(y, 4)),
          Broadcast::make(Min::make(x, y), 4));
    check(Min::make(x, x+3), x);
    check(Min::make(x+4, x), x);
    check(Min::make(x-1, x+2), x+(-1));
    check(Min::make(7, Min::make(x, 3)), Min::make(x, 3));
    check(Min::make(Min::make(x, y), x), Min::make(x, y));
    check(Min::make(Min::make(x, y), y), Min::make(x, y));
    check(Min::make(x, Min::make(x, y)), Min::make(x, y));
    check(Min::make(y, Min::make(x, y)), Min::make(x, y));

    check(Max::make(7, 3), 7);
    check(Max::make(4.25f, 1.25f), 4.25f);
    check(Max::make(Broadcast::make(x, 4), Broadcast::make(y, 4)),
          Broadcast::make(Max::make(x, y), 4));
    check(Max::make(x, x+3), x+3);
    check(Max::make(x+4, x), x+4);
    check(Max::make(x-1, x+2), x+2);
    check(Max::make(7, Max::make(x, 3)), Max::make(x, 7));
    check(Max::make(Max::make(x, y), x), Max::make(x, y));
    check(Max::make(Max::make(x, y), y), Max::make(x, y));
    check(Max::make(x, Max::make(x, y)), Max::make(x, y));
    check(Max::make(y, Max::make(x, y)), Max::make(x, y));

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

    // Check that simplifier can recognise instances where the extremes of the
    // datatype appear as constants in comparisons, Min and Max expressions.
    // The result of min/max with extreme is known to be either the extreme or
    // the other expression.  The result of < or > comparison is known to be true or false.
    check(x <= Int(32).max(), const_true());
    check(Cast::make(Int(16), x) >= Int(16).min(), const_true());
    check(x < Int(32).min(), const_false());
    check(Min::make(Cast::make(UInt(16), x), Cast::make(UInt(16), 65535)), Cast::make(UInt(16), x));
    check(Min::make(x, Int(32).max()), x);
    check(Min::make(Int(32).min(), x), Int(32).min());
    check(Max::make(Cast::make(Int(8), x), Cast::make(Int(8), -128)), Cast::make(Int(8), x));
    check(Max::make(x, Int(32).min()), x);
    check(Max::make(x, Int(32).max()), Int(32).max());
    // Check that non-extremes do not lead to incorrect simplification
    check(Max::make(Cast::make(Int(8), x), Cast::make(Int(8), -127)), Max::make(Cast::make(Int(8), x), Cast::make(Int(8), -127)));

    // Check an optimization important for fusing dimensions
    check((x/3)*3 + x%3, x);
    check(x%3 + (x/3)*3, x);

    check(((x/3)*3 + y) + x%3, x + y);
    check((x%3 + y) + (x/3)*3, x + y);

    check((y + x%3) + (x/3)*3, y + x);
    check((y + (x/3*3)) + x%3, y + x);

    // Check bitshift operations
    check(Cast::make(Int(16), x) << 10, Cast::make(Int(16), x) * 1024);
    check(Cast::make(Int(16), x) >> 10, Cast::make(Int(16), x) / 1024);
    check(Cast::make(Int(16), x) << -10, Cast::make(Int(16), x) / 1024);
    // Correctly triggers a warning:
    //check(Cast::make(Int(16), x) << 20, Cast::make(Int(16), x) << 20);

    // Check that chains of widening casts don't lose the distinction
    // between zero-extending and sign-extending.
    check(Cast::make(UInt(64), Cast::make(UInt(32), Cast::make(Int(8), -1))),
          Cast::make(UInt(64), Cast::make(UInt(32), -1)));


    // Some quaternary rules with cancellations
    check((x + y) - (z + y), x - z);
    check((x + y) - (y + z), x - z);
    check((y + x) - (z + y), x - z);
    check((y + x) - (y + z), x - z);

    check((x - y) - (z - y), x - z);
    check((y - z) - (y - x), x - z);

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

    check(log(0.5f + 0.5f), 0.0f);
    check(exp(log(2.0f)), 2.0f);

    check(floor(0.98f), 0.0f);
    check(ceil(0.98f), 1.0f);
    check(round(0.6f), 1.0f);
    check(round(-0.5f), 0.0f);
    check(trunc(-1.6f), -1.0f);
    check(floor(round(x)), round(x));
    check(ceil(ceil(x)), ceil(x));

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

    // The min of two matching clamps is the clamp of the mins
    check(min(clamp(x, -10, 14), clamp(y, -10, 14)), clamp(min(x, y), -10, 14));

    check(Ramp::make(0, 1, 4) == Broadcast::make(2, 4),
          Ramp::make(-2, 1, 4) == Broadcast::make(0, 4));

    check(min(x/4, y/4), min(x, y)/4);
    check(max(x/4, y/4), max(x, y)/4);

    check(min(x/(-4), y/(-4)), max(x, y)/(-4));
    check(max(x/(-4), y/(-4)), min(x, y)/(-4));

    check(!f, t);
    check(!t, f);
    check(!(x < y), y <= x);
    check(!(x > y), x <= y);
    check(!(x >= y), x < y);
    check(!(x <= y), y < x);
    check(!(x == y), x != y);
    check(!(x != y), x == y);
    check(!(!(x == 0)), x == 0);
    check(!Expr(Broadcast::make(x > y, 4)),
          Broadcast::make(x <= y, 4));

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

    check((x/8)*8 < x - 8, f);
    check((x/8)*8 < x - 9, f);
    check((x/8)*8 < x - 7, (x/8)*8 < x + (-7));
    check(Ramp::make(x*8, 1, 4) < Broadcast::make(y*8, 4), Broadcast::make(x < y, 4));
    check(Ramp::make(x*8, 2, 4) < Broadcast::make(y*8, 4), Broadcast::make(x < y, 4));
    check(Ramp::make(x*8, 3, 4) < Broadcast::make(y*8, 4), Ramp::make(x*8, 3, 4) < Broadcast::make(y*8, 4));

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

    // Check anded conditions apply to the then case only
    check(IfThenElse::make(x == 4 && y == 5,
                           Evaluate::make(z + x + y),
                           Evaluate::make(z + x - y)),
          IfThenElse::make(x == 4 && y == 5,
                           Evaluate::make(z + 9),
                           Evaluate::make(z + x - y)));

    // Check ored conditions apply to the else case only
    Expr b1 = Variable::make(Bool(), "b1");
    Expr b2 = Variable::make(Bool(), "b2");
    check(IfThenElse::make(b1 || b2,
                           Evaluate::make(Select::make(b1, x+3, x+4) + Select::make(b2, x+5, x+7)),
                           Evaluate::make(Select::make(b1, x+3, x+8) - Select::make(b2, x+5, x+7))),
          IfThenElse::make(b1 || b2,
                           Evaluate::make(Select::make(b1, x+3, x+4) + Select::make(b2, x+5, x+7)),
                           Evaluate::make(1)));

    // Check single conditions apply to both cases of an ifthenelse
    check(IfThenElse::make(b1,
                           Evaluate::make(Select::make(b1, x, y)),
                           Evaluate::make(Select::make(b1, z, w))),
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

    check(b1 || !b1, t);
    check(!b1 || b1, t);
    check(b1 && !b1, f);
    check(!b1 && b1, f);
    check(b1 && b1, b1);
    check(b1 || b1, b1);
    check(Broadcast::make(b1, 4) || Broadcast::make(!b1, 4), Broadcast::make(t, 4));
    check(Broadcast::make(!b1, 4) || Broadcast::make(b1, 4), Broadcast::make(t, 4));
    check(Broadcast::make(b1, 4) && Broadcast::make(!b1, 4), Broadcast::make(f, 4));
    check(Broadcast::make(!b1, 4) && Broadcast::make(b1, 4), Broadcast::make(f, 4));
    check(Broadcast::make(b1, 4) && Broadcast::make(b1, 4), Broadcast::make(b1, 4));
    check(Broadcast::make(b1, 4) || Broadcast::make(b1, 4), Broadcast::make(b1, 4));

    v = Variable::make(Int(32, 4), "v");
    // Check constants get pushed inwards
    check(Let::make("x", 3, x+4), 7);

    // Check ramps in lets get pushed inwards
    check(Let::make("v", Ramp::make(x*2+7, 3, 4), v + Expr(Broadcast::make(2, 4))),
          Ramp::make(x*2+9, 3, 4));

    // Check broadcasts in lets get pushed inwards
    check(Let::make("v", Broadcast::make(x, 4), v + Expr(Broadcast::make(2, 4))),
          Broadcast::make(x+2, 4));

    // Check that dead lets get stripped
    check(Let::make("x", 3*y*y*y, 4), 4);
    check(Let::make("x", 0, 0), 0);

    // Test case with most negative 32-bit number, as constant to check that it is not negated.
    check(((x * (int32_t)0x80000000) + (y + z * (int32_t)0x80000000)),
          ((x * (int32_t)0x80000000) + (y + z * (int32_t)0x80000000)));

    // Check that constant args to a stringify get combined
    check(Call::make(Handle(), Call::stringify, {3, string(" "), 4}, Call::Intrinsic),
          string("3 4"));

    check(Call::make(Handle(), Call::stringify, {3, x, 4, string(", "), 3.4f}, Call::Intrinsic),
          Call::make(Handle(), Call::stringify, {string("3"), x, string("4, 3.400000")}, Call::Intrinsic));

    // Check if we can simplify away comparison on vector types considering bounds.
    Scope<Interval> bounds_info;
    bounds_info.push("x", Interval(0,4));
    check_in_bounds(Ramp::make(x, 1,4) < Broadcast::make(0,4),  const_false(4), bounds_info);
    check_in_bounds(Ramp::make(x, 1,4) < Broadcast::make(8,4),  const_true(4),  bounds_info);
    check_in_bounds(Ramp::make(x,-1,4) < Broadcast::make(-4,4), const_false(4), bounds_info);
    check_in_bounds(Ramp::make(x,-1,4) < Broadcast::make(5,4),  const_true(4),  bounds_info);

    // min and max on constant ramp v broadcast
    check(max(Ramp::make(0, 1, 8), 0), Ramp::make(0, 1, 8));
    check(min(Ramp::make(0, 1, 8), 7), Ramp::make(0, 1, 8));
    check(max(Ramp::make(0, 1, 8), 7), Broadcast::make(7, 8));
    check(min(Ramp::make(0, 1, 8), 0), Broadcast::make(0, 8));
    check(min(Ramp::make(0, 1, 8), 4), min(Ramp::make(0, 1, 8), 4));

    check(max(Ramp::make(7, -1, 8), 0), Ramp::make(7, -1, 8));
    check(min(Ramp::make(7, -1, 8), 7), Ramp::make(7, -1, 8));
    check(max(Ramp::make(7, -1, 8), 7), Broadcast::make(7, 8));
    check(min(Ramp::make(7, -1, 8), 0), Broadcast::make(0, 8));
    check(min(Ramp::make(7, -1, 8), 4), min(Ramp::make(7, -1, 8), 4));

    check(max(0, Ramp::make(0, 1, 8)), Ramp::make(0, 1, 8));
    check(min(7, Ramp::make(0, 1, 8)), Ramp::make(0, 1, 8));

    check(min(8 - x, 2), 8 - max(x, 6));
    check(max(3, 77 - x), 77 - min(x, 74));
    check(min(max(8-x, 0), 8), 8 - max(min(x, 8), 0));

    std::cout << "Simplify test passed" << std::endl;
}
}
}
