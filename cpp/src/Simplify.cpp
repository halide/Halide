#include "Simplify.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRPrinter.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Var.h"
#include "Log.h"
#include "ModulusRemainder.h"
#include <iostream>

namespace Halide { 
namespace Internal {

using std::string;

bool is_simple_const(Expr e) {
    return is_const(e) && (!e.as<Cast>());
}

// Is a constant representable as a certain type
int do_indirect_int_cast(Type t, int x) {
    if (t == Int(8)) {
        return (int8_t)x;
    } else if (t == UInt(8)) {
        return (uint8_t)x;
    } else if (t == Int(16)) {
        return (int16_t)x;
    } else if (t == UInt(16)) {
        return (uint16_t)x;
    } else if (t == UInt(32)) {
        return (uint32_t)x;
    } else if (t.is_int()) {
        return x;
    } else if (t == Float(32)) {
        return (float)x;
    } else if (t == Float(64)) {
        return (double)x;
    } else {
        assert(false && "Can't do an indirect int cast via this type");
        return 0;
    }
}

class Simplify : public IRMutator {

    Scope<Expr> scope;

    Scope<ModulusRemainder> alignment_info;

    using IRMutator::visit;

    void visit(const IntImm *op) {
        IRMutator::visit(op);
    }

    void visit(const FloatImm *op) {
        IRMutator::visit(op);
    }

    bool const_float(Expr e, float *f) {
        const FloatImm *c = e.as<FloatImm>();
        if (c) {
            *f = c->value;
            return true;
        } else {
            return false;
        }
    }

    bool const_int(Expr e, int *i) {
        const IntImm *c = e.as<IntImm>();
        if (c) {
            *i = c->value;
            return true;
        } else {
            return false;
        }
    }

    void visit(const Cast *op) {
        Expr value = mutate(op->value);        
        const Cast *cast = value.as<Cast>();
        float f;
        int i;
        if (value.type() == op->type) {
            expr = value;
        } else if (op->type == Int(32) && const_float(value, &f)) {
            expr = new IntImm((int)f);
        } else if (op->type == Float(32) && const_int(value, &i)) {
            expr = new FloatImm((float)i);
        } else if (op->type == Int(32) && cast && const_int(cast->value, &i)) {
            // Cast to something then back to int
            expr = do_indirect_int_cast(cast->type, i);
        } else if (value.same_as(op->value)) {
            expr = op;
        } else {
            expr = new Cast(op->type, value);
        }
    }

    void visit(const Variable *op) {
        if (scope.contains(op->name)) {
            Expr replacement = scope.get(op->name);

            //std::cout << "Pondering replacing " << op->name << " with " << replacement << std::endl;

            // if expr is defined, we should substitute it in (unless
            // it's a var that has been hidden by a nested scope).
            if (replacement.defined()) {
                //std::cout << "Replacing " << op->name << " of type " << op->type << " with " << replacement << std::endl;
                assert(replacement.type() == op->type);
                // If it's a naked var, and the var it refers to
                // hasn't gone out of scope, just replace it with that
                // var
                if (const Variable *v = replacement.as<Variable>()) {
                    if (scope.contains(v->name)) {
                        if (scope.depth(v->name) < scope.depth(op->name)) {
                            expr = replacement;
                        } else {
                            // Uh oh, the variable we were going to
                            // subs in has been hidden by another
                            // variable of the same name, better not
                            // do anything.
                            expr = op;
                        }
                    } else {
                        // It is a variable, but the variable this
                        // refers to hasn't been encountered. It must
                        // be a uniform, so it's safe to substitute it
                        // in.
                        expr = replacement;
                    }
                } else {
                    // It's not a variable, and a replacement is defined
                    expr = replacement;
                }
            } else {
                // This expression was not something deemed
                // substitutable - no replacement is defined.
                expr = op;
            }
        } else {
            // We never encountered a let that defines this var. Must
            // be a uniform. Don't touch it.
            expr = op;
        }
    }

    void visit(const Add *op) {
        log(3) << "Simplifying " << Expr(op) << "\n";

        int ia, ib;
        float fa, fb;

        Expr a = mutate(op->a), b = mutate(op->b);

        // rearrange const + varying to varying + const, to cut down
        // on cases to check
        if (is_simple_const(a) && !is_simple_const(b)) std::swap(a, b);

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
        } else if (ramp_a && ramp_b) {
            // Ramp + Ramp
            expr = mutate(new Ramp(ramp_a->base + ramp_b->base,
                                   ramp_a->stride + ramp_b->stride, ramp_a->width));
        } else if (ramp_a && broadcast_b) {
            // Ramp + Broadcast
            expr = mutate(new Ramp(ramp_a->base + broadcast_b->value, 
                                   ramp_a->stride, ramp_a->width));
        } else if (broadcast_a && ramp_b) {
            // Broadcast + Ramp
            expr = mutate(new Ramp(broadcast_a->value + ramp_b->base, 
                                   ramp_b->stride, ramp_b->width));
        } else if (broadcast_a && broadcast_b) {
            // Broadcast + Broadcast
            expr = new Broadcast(mutate(broadcast_a->value + broadcast_b->value),
                                 broadcast_a->width);
        } else if (add_a && is_simple_const(add_a->b)) {
            // In ternary expressions, pull constants outside
            if (is_simple_const(b)) expr = mutate(add_a->a + (add_a->b + b));
            else expr = mutate((add_a->a + b) + add_a->b);
        } else if (add_b && is_simple_const(add_b->b)) {
            expr = mutate((a + add_b->a) + add_b->b);
        } else if (sub_a && is_simple_const(sub_a->a) && is_simple_const(b)) {
            expr = mutate((sub_a->a + b) - sub_a->b);
        } else if (sub_a && equal(b, sub_a->b)) {
            // Additions that cancel an inner term
            expr = sub_a->a;
        } else if (sub_b && equal(a, sub_b->b)) {            
            expr = sub_b->a;
        } else if (mul_a && mul_b && equal(mul_a->a, mul_b->a)) {
            // Pull out common factors a*x + b*x
            expr = mutate(mul_a->a * (mul_a->b + mul_b->b));
        } else if (mul_a && mul_b && equal(mul_a->b, mul_b->a)) {
            expr = mutate(mul_a->b * (mul_a->a + mul_b->b));
        } else if (mul_a && mul_b && equal(mul_a->b, mul_b->b)) {
            expr = mutate(mul_a->b * (mul_a->a + mul_b->a));
        } else if (mul_a && mul_b && equal(mul_a->a, mul_b->b)) {
            expr = mutate(mul_a->a * (mul_a->b + mul_b->a));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            // If we've made no changes, and can't find a rule to apply, return the operator unchanged.
            expr = op;
        } else {
            expr = new Add(a, b);
        }
    }

    void visit(const Sub *op) {
        log(3) << "Simplifying " << Expr(op) << "\n";

        Expr a = mutate(op->a), b = mutate(op->b);

        int ia, ib; 
        float fa, fb;

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
        } else if (ramp_a && ramp_b) {
            // Ramp - Ramp
            expr = mutate(new Ramp(ramp_a->base - ramp_b->base,
                                   ramp_a->stride - ramp_b->stride, ramp_a->width));
        } else if (ramp_a && broadcast_b) {
            // Ramp - Broadcast
            expr = mutate(new Ramp(ramp_a->base - broadcast_b->value, 
                                   ramp_a->stride, ramp_a->width));
        } else if (broadcast_a && ramp_b) {
            // Broadcast - Ramp
            expr = mutate(new Ramp(broadcast_a->value - ramp_b->base, 
                                   make_zero(ramp_b->stride.type())- ramp_b->stride,
                                   ramp_b->width));
        } else if (broadcast_a && broadcast_b) {
            // Broadcast + Broadcast
            expr = new Broadcast(mutate(broadcast_a->value - broadcast_b->value),
                                 broadcast_a->width);
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
            if (is_simple_const(b)) expr = mutate(add_a->a + (add_a->b - b));
            else expr = mutate((add_a->a - b) + add_a->b);
        } else if (add_b && is_simple_const(add_b->b)) {
            expr = mutate((a - add_b->a) - add_b->b);
        } else if (sub_a && is_simple_const(sub_a->a) && is_simple_const(b)) {
            expr = mutate((sub_a->a - b) - sub_a->b);
        } else if (sub_b && is_simple_const(sub_b->b)) {
            if (is_simple_const(a)) expr = mutate((a + sub_b->b) - sub_b->a);
            expr = mutate((a - sub_b->a) + sub_b->b);
        } else if (mul_a && mul_b && equal(mul_a->a, mul_b->a)) {
            // Pull out common factors a*x + b*x
            expr = mutate(mul_a->a * (mul_a->b - mul_b->b));
        } else if (mul_a && mul_b && equal(mul_a->b, mul_b->a)) {
            expr = mutate(mul_a->b * (mul_a->a - mul_b->b));
        } else if (mul_a && mul_b && equal(mul_a->b, mul_b->b)) {
            expr = mutate(mul_a->b * (mul_a->a - mul_b->a));
        } else if (mul_a && mul_b && equal(mul_a->a, mul_b->b)) {
            expr = mutate(mul_a->a * (mul_a->b - mul_b->a));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Sub(a, b);
        }
    }

    void visit(const Mul *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        if (is_simple_const(a)) std::swap(a, b);

        int ia, ib; 
        float fa, fb;

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Mul *mul_a = a.as<Mul>();

        // TODO: broadcast * broadcast, ramp * broadcast, broadcast * ramp
        // (a * const) * const
        // (a + const) * const

        if (is_zero(b)) {
            expr = b;
        } else if (is_one(b)) {
            expr = a;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            expr = ia*ib;
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = fa*fb;
        } else if (broadcast_a && broadcast_b) {
            expr = new Broadcast(mutate(broadcast_a->value * broadcast_b->value), broadcast_a->width);
        } else if (ramp_a && broadcast_b) {
            Expr m = broadcast_b->value;
            expr = mutate(new Ramp(ramp_a->base * m, ramp_a->stride * m, ramp_a->width));
        } else if (broadcast_a && ramp_b) {
            Expr m = broadcast_a->value;
            expr = mutate(new Ramp(m * ramp_b->base, m * ramp_b->stride, ramp_b->width));
        } else if (add_a && is_simple_const(add_a->b) && is_simple_const(b)) {
            expr = mutate(add_a->a * b + add_a->b * b);
        } else if (mul_a && is_simple_const(mul_a->b) && is_simple_const(b)) {
            expr = mutate(mul_a->a * (mul_a->b * b));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Mul(a, b);
        }
    }

    void visit(const Div *op) {
        Expr a = mutate(op->a), b = mutate(op->b);
        
        int ia, ib;
        float fa, fb;

        const Mul *mul_a = a.as<Mul>();
        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Div *div_a = a.as<Div>();
        const Mul *mul_a_a = NULL;
        const Mul *mul_a_b = NULL;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Ramp *ramp_a = a.as<Ramp>();
        const Broadcast *broadcast_b = b.as<Broadcast>();

        if (add_a) {
            mul_a_a = add_a->a.as<Mul>();
            mul_a_b = add_a->b.as<Mul>();
        } else if (sub_a) {
            mul_a_a = sub_a->a.as<Mul>();
            mul_a_b = sub_a->b.as<Mul>();
        }

        if (is_zero(a)) {
            expr = a;
        } else if (is_one(b)) {
            expr = a;
        } else if (equal(a, b)) {
            expr = make_one(a.type());
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            expr = ia/ib;
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = fa/fb;
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(new Broadcast(broadcast_a->value / broadcast_b->value, broadcast_a->width));
        } else if (ramp_a && broadcast_b && 
                   const_int(broadcast_b->value, &ib) && 
                   const_int(ramp_a->stride, &ia) && ((ia % ib) == 0)) {
            // ramp(x, ia, w) / broadcast(ib, w) -> ramp(x/ib, ia/ib, w) when ib divides ia
            expr = mutate(new Ramp(ramp_a->base/ib, ia/ib, ramp_a->width));
        } else if (div_a && const_int(div_a->b, &ia) && const_int(b, &ib)) {
            // (x / 3) / 4 -> x / 12
            expr = mutate(div_a->a / (ia*ib));
        } else if (mul_a && const_int(mul_a->b, &ia) && const_int(b, &ib) && 
                   ia && ib && (ia % ib == 0 || ib % ia == 0)) {
            if (ia % ib == 0) {
                // (x * 4) / 2 -> x * 2
                expr = mutate(mul_a->a * (ia / ib));
            } else {
                // (x * 2) / 4 -> x / 2
                expr = mutate(mul_a->a / (ib / ia));
            }            
        } else if (add_a && mul_a_a && const_int(mul_a_a->b, &ia) && const_int(b, &ib) && 
                   ib && (ia % ib == 0)) {
            // Pull terms that are a multiple of the divisor out
            // (x*4 + y) / 2 -> x*2 + y/2            
            expr = mutate((mul_a_a->a * (ia/ib)) + (add_a->b / b));
        } else if (add_a && mul_a_b && const_int(mul_a_b->b, &ia) && const_int(b, &ib) && 
                   ib && (ia % ib == 0)) {
            // (y + x*4) / 2 -> y/2 + x*2
            expr = mutate((add_a->a / b) + (mul_a_b->a * (ia/ib)));
        } else if (sub_a && mul_a_a && const_int(mul_a_a->b, &ia) && const_int(b, &ib) && 
                   ib && (ia % ib == 0)) {
            // Pull terms that are a multiple of the divisor out
            // (x*4 - y) / 2 -> x*2 - y/2            
            expr = mutate((mul_a_a->a * (ia/ib)) - (sub_a->b / b));
        } else if (sub_a && mul_a_b && const_int(mul_a_b->b, &ia) && const_int(b, &ib) && 
                   ib && (ia % ib == 0)) {
            // (y - x*4) / 2 -> y/2 - x*2
            expr = mutate((sub_a->a / b) - (mul_a_b->a * (ia/ib)));
        } else if (b.type().is_float() && is_simple_const(b)) {
            // Convert const float division to multiplication
            // x / 2 -> x * 0.5
            expr = mutate(a * (make_one(b.type()) / b));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Div(a, b);
        }
    }

    void visit(const Mod *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        int ia, ib;
        float fa, fb;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Mul *mul_a = a.as<Mul>();
        const Add *add_a = a.as<Add>();
        const Mul *mul_a_a = add_a ? add_a->a.as<Mul>() : NULL;
        const Mul *mul_a_b = add_a ? add_a->b.as<Mul>() : NULL;

        // If the RHS is a constant, do modulus remainder analysis on the LHS
        ModulusRemainder mod_rem(0, 1);
        if (const_int(b, &ib) && a.type() == Int(32)) {
            mod_rem = modulus_remainder(a, alignment_info);
        }

        if (const_int(a, &ia) && const_int(b, &ib)) {
            int i = ia % ib;
            if (i < 0) i += ib;
            expr = i;
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            float f = fa - fb * ((int)(fa / fb));
            if (f < 0) f += fb;
            expr = f;
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(new Broadcast(broadcast_a->value % broadcast_b->value, broadcast_a->width));
        } else if (mul_a && const_int(b, &ib) && const_int(mul_a->b, &ia) && (ia % ib == 0)) {
            // (x * (b*a)) % b -> 0
            expr = make_zero(a.type());
        } else if (add_a && mul_a_a && const_int(mul_a_a->b, &ia) && const_int(b, &ib) && (ia % ib == 0)) {
            // (x * (b*a) + y) % b -> y
            expr = add_a->b;
        } else if (add_a && mul_a_b && const_int(mul_a_b->b, &ia) && const_int(b, &ib) && (ia % ib == 0)) {
            // (y + x * (b*a)) % b -> y
            expr = add_a->a;
        } else if (const_int(b, &ib) && a.type() == Int(32) && mod_rem.modulus % ib == 0) {
            // ((a*b)*x + c) % a -> c
            expr = mod_rem.remainder;
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Mod(a, b);
        }
    }

    void visit(const Min *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        // Move constants to the right to cut down on number of cases to check
        if (is_simple_const(a) && !is_simple_const(b)) {
            std::swap(a, b);
        }

        int ia, ib;
        float fa, fb;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Min *min_a = a.as<Min>();
        const Min *min_b = b.as<Min>();
        const Min *min_a_a = min_a ? min_a->a.as<Min>() : NULL;

        if (equal(a, b)) {
            expr = a;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            expr = std::min(ia, ib);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = std::min(fa, fb);
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(new Broadcast(new Min(broadcast_a->value, broadcast_b->value), broadcast_a->width));
        } else if (add_a && const_int(add_a->b, &ia) && 
                   add_b && const_int(add_b->b, &ib) && 
                   equal(add_a->a, add_b->a)) {
            // min(x + 3, x - 2) -> x - 2
            if (ia > ib) {
                expr = b;
            } else {
                expr = a;
            }
        } else if (add_a && const_int(add_a->b, &ia) && equal(add_a->a, b)) {
            // min(x + 5, x)
            if (ia > 0) {
                expr = b;
            } else {
                expr = a;
            }
        } else if (add_b && const_int(add_b->b, &ib) && equal(add_b->a, a)) {
            // min(x, x + 5)
            if (ib > 0) {
                expr = a;
            } else {
                expr = b;
            }
        } else if (min_a && is_simple_const(min_a->b) && is_simple_const(b)) {
            // min(min(x, 4), 5) -> min(x, 4)
            expr = new Min(min_a->a, mutate(new Min(min_a->b, b)));
        } else if (min_a && (equal(min_a->b, b) || equal(min_a->a, b))) {
            // min(min(x, y), y) -> min(x, y)
            expr = a;
        } else if (min_b && (equal(min_b->b, a) || equal(min_b->a, a))) {
            // min(y, min(x, y)) -> min(x, y)
            expr = b;            
        } else if (min_a_a && equal(min_a_a->b, b)) {
            // min(min(min(x, y), z), y) -> min(min(x, y), z)
            expr = a;            
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Min(a, b);
        }
    }

    void visit(const Max *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        // Move constants to the right to cut down on number of cases to check
        if (is_simple_const(a) && !is_simple_const(b)) {
            std::swap(a, b);
        }

        int ia, ib;
        float fa, fb;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();
        const Max *max_a_a = max_a ? max_a->a.as<Max>() : NULL;

        if (equal(a, b)) {
            expr = a;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            expr = std::max(ia, ib);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = std::max(fa, fb);
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(new Broadcast(new Max(broadcast_a->value, broadcast_b->value), broadcast_a->width));
        } else if (add_a && const_int(add_a->b, &ia) && add_b && const_int(add_b->b, &ib) && equal(add_a->a, add_b->a)) {
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
        } else if (max_a && is_simple_const(max_a->b) && is_simple_const(b)) {
            // max(max(x, 4), 5) -> max(x, 5)
            expr = new Max(max_a->a, mutate(new Max(max_a->b, b)));
        } else if (max_a && (equal(max_a->b, b) || equal(max_a->a, b))) {
            // max(max(x, y), y) -> max(x, y)
            expr = a;
        } else if (max_b && (equal(max_b->b, a) || equal(max_b->a, a))) {
            // max(y, max(x, y)) -> max(x, y)
            expr = b;            
        } else if (max_a_a && equal(max_a_a->b, b)) {
            // max(max(max(x, y), z), y) -> max(max(x, y), z)
            expr = a;            
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Max(a, b);
        }
    }

    void visit(const EQ *op) {
        Expr a = mutate(op->a), b = mutate(op->b);
        Expr delta = mutate(a - b);

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

        if (is_zero(delta)) {
            expr = const_true(op->type.width);
        } else if (is_simple_const(delta)) {
            expr = const_false(op->type.width);
        } else if (is_simple_const(a) && !is_simple_const(b)) {
            // Move constants to the right
            expr = mutate(b == a);
        } else if (broadcast_a && broadcast_b) {
            // Push broadcasts outwards
            expr = mutate(new Broadcast(broadcast_a->value == broadcast_b->value, broadcast_a->width));
        } else if (ramp_a && ramp_b && equal(ramp_a->stride, ramp_b->stride)) {
            // Ramps with matching stride
            Expr bases_match = (ramp_a->base == ramp_b->base);
            expr = mutate(new Broadcast(bases_match, ramp_a->width));
        } else if (add_a && add_b && equal(add_a->a, add_b->a)) {
            // Subtract a term from both sides
            expr = mutate(add_a->b == add_b->b);
        } else if (add_a && add_b && equal(add_a->a, add_b->b)) {
            expr = mutate(add_a->b == add_b->a);
        } else if (add_a && add_b && equal(add_a->b, add_b->a)) {
            expr = mutate(add_a->a == add_b->b);
        } else if (add_a && add_b && equal(add_a->b, add_b->b)) {
            expr = mutate(add_a->a == add_b->a);
        } else if (sub_a && sub_b && equal(sub_a->a, sub_b->a)) {
            // Add a term to both sides
            expr = mutate(sub_a->b == sub_b->b);
        } else if (sub_a && sub_b && equal(sub_a->b, sub_b->b)) {
            expr = mutate(sub_a->a == sub_b->a);
        } else if (add_a) {
            // Rearrange so that all adds and subs are on the rhs to cut down on further cases
            expr = mutate(add_a->a == (b - add_a->b));
        } else if (sub_a) {
            expr = mutate(sub_a->a == (b + sub_a->b));
        } else if (add_b && equal(add_b->a, a)) {
            // Subtract a term from both sides
            expr = mutate(make_zero(add_b->b.type()) == add_b->b);
        } else if (add_b && equal(add_b->b, a)) {
            expr = mutate(make_zero(add_b->a.type()) == add_b->a);
        } else if (sub_b && equal(sub_b->a, a)) {
            // Add a term to both sides
            expr = mutate(make_zero(sub_b->b.type()) == sub_b->b);
        } else if (mul_a && mul_b && is_simple_const(mul_a->b) && is_simple_const(mul_b->b) && equal(mul_a->b, mul_b->b)) {
            // Divide both sides by a constant
            assert(!is_zero(mul_a->b) && "Multiplication by zero survived constant folding");
            expr = mutate(mul_a->a == mul_b->a);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new EQ(a, b);
        }
    }

    void visit(const NE *op) {
        expr = mutate(new Not(op->a == op->b));
    }

    void visit(const LT *op) {
        Expr a = mutate(op->a), b = mutate(op->b);
        Expr delta = mutate(a - b);

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

        if (is_zero(delta) || is_positive_const(delta)) {
            expr = const_false(op->type.width);
        } else if (is_negative_const(delta)) {
            expr = const_true(op->type.width);
        } else if (broadcast_a && broadcast_b) {
            // Push broadcasts outwards
            expr = mutate(new Broadcast(broadcast_a->value < broadcast_b->value, broadcast_a->width));
        } else if (ramp_a && ramp_b && equal(ramp_a->stride, ramp_b->stride)) {
            // Ramps with matching stride
            Expr bases_lt = (ramp_a->base < ramp_b->base);
            expr = mutate(new Broadcast(bases_lt, ramp_a->width));
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
            // Add a term to both sides
            expr = mutate(sub_a->b < sub_b->b);
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
        } else if (sub_b && equal(sub_b->a, a)) {
            // Add a term to both sides
            expr = mutate(sub_b->b < make_zero(sub_b->b.type()));
        } else if (mul_a && mul_b && 
                   is_positive_const(mul_a->b) && is_positive_const(mul_b->b) && 
                   equal(mul_a->b, mul_b->b)) {
            // Divide both sides by a constant
            expr = mutate(mul_a->a < mul_b->a);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new LT(a, b);
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
        Expr a = mutate(op->a), b = mutate(op->b);

        if (is_one(op->a)) {
            expr = op->b;
        } else if (is_one(op->b)) {
            expr = op->a;
        } else if (is_zero(op->a)) {
            expr = op->a;
        } else if (is_zero(op->b)) {
            expr = op->b;
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new And(a, b);
        }
    }

    void visit(const Or *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        if (is_one(op->a)) {
            expr = op->a;
        } else if (is_one(op->b)) {
            expr = op->b;
        } else if (is_zero(op->a)) {
            expr = op->b;
        } else if (is_zero(op->b)) {
            expr = op->a;
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Or(a, b);
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
            expr = new LT(n->b, n->a);
        } else if (const GE *n = a.as<GE>()) {
            expr = new LT(n->a, n->b);
        } else if (const LT *n = a.as<LT>()) {
            expr = new LE(n->b, n->a);
        } else if (const GT *n = a.as<GT>()) {
            expr = new LE(n->a, n->b);
        } else if (const NE *n = a.as<NE>()) {
            expr = new EQ(n->a, n->b);
        } else if (const EQ *n = a.as<EQ>()) {
            expr = new NE(n->a, n->b);
        } else if (const Broadcast *n = a.as<Broadcast>()) {
            expr = mutate(new Broadcast(!n->value, n->width));
        } else if (a.same_as(op->a)) {
            expr = op;
        } else {
            expr = new Not(a);
        }
    }

    void visit(const Select *op) {
        Expr condition = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);

        if (is_one(condition)) {
            expr = true_value;
        } else if (is_zero(condition)) {
            expr = false_value;
        } else if (equal(true_value, false_value)) {
            expr = true_value;
        } else if (const NE *ne = condition.as<NE>()) {
            // Normalize select(a != b, c, d) to select(a == b, d, c) 
            expr = mutate(new Select(ne->a == ne->b, false_value, true_value));               
        } else if (const LE *le = condition.as<LE>()) {
            // Normalize select(a <= b, c, d) to select(b < a, d, c) 
            expr = mutate(new Select(le->b < le->a, false_value, true_value));               
        } else if (condition.same_as(op->condition) &&
                   true_value.same_as(op->true_value) &&
                   false_value.same_as(op->false_value)) {
            expr = op;
        } else {
            expr = new Select(condition, true_value, false_value);
        }
    }

    void visit(const Load *op) {
        IRMutator::visit(op);
    }

    void visit(const Ramp *op) {
        IRMutator::visit(op);
    }

    void visit(const Broadcast *op) {
        IRMutator::visit(op);
    }

    void visit(const Call *op) {
        IRMutator::visit(op);
    }

    template<typename T, typename Body> 
    Body simplify_let(const T *op, Scope<Expr> &scope, IRMutator *mutator) {
        // If the value is trivial, make a note of it in the scope so
        // we can subs it in later
        Expr value = mutator->mutate(op->value);
        Body body = op->body;
        assert(value.defined());
        assert(body.defined());
        const Ramp *ramp = value.as<Ramp>();
        const Broadcast *broadcast = value.as<Broadcast>();        
        const Variable *var = value.as<Variable>();
        string wrapper_name;
        Expr wrapper_value;
        if (is_simple_const(value)) {
            // Substitute the value wherever we see it
            scope.push(op->name, value);
        } else if (ramp && is_simple_const(ramp->stride)) {
            // Make a new name to refer to the base instead, and push the ramp inside

            Expr val = new Variable(ramp->base.type(), op->name + ".base");
            Expr base = ramp->base;

            // If it's a multiply, move the multiply part inwards
            const Mul *mul = base.as<Mul>();
            const IntImm *mul_b = mul ? mul->b.as<IntImm>() : NULL;
            if (mul_b) {
                base = mul->a;
                val = new Ramp(val * mul->b, ramp->stride, ramp->width);
            } else {
                val = new Ramp(val, ramp->stride, ramp->width);
            }

            scope.push(op->name, val);

            wrapper_name = op->name + ".base";
            wrapper_value = base;
        } else if (broadcast) {
            // Make a new name refer to the scalar version, and push the broadcast inside            
            scope.push(op->name, new Broadcast(new Variable(broadcast->value.type(), op->name + ".value"), 
                                               broadcast->width));
            wrapper_name = op->name + ".value";
            wrapper_value = broadcast->value;
        } else if (var) {
            // This var is just equal to another var. We should subs
            // it in only if the second var is still in scope at the
            // usage site (this is checked in the visit(Variable*) method.
            scope.push(op->name, var);
        } else {
            // Push a empty expr on, to make sure we hide anything
            // else with the same name until this goes out of scope
            scope.push(op->name, Expr());
        }

        // Before we enter the body, track the alignment info 
        bool wrapper_tracked = false;
        if (wrapper_value.defined() && wrapper_value.type() == Int(32)) {
            ModulusRemainder mod_rem = modulus_remainder(wrapper_value, alignment_info);
            alignment_info.push(wrapper_name, mod_rem);
            wrapper_tracked = true;
        }

        bool value_tracked = false;
        if (value.type() == Int(32)) {
            ModulusRemainder mod_rem = modulus_remainder(value, alignment_info);
            alignment_info.push(op->name, mod_rem);
            value_tracked = true;
        }

        body = mutator->mutate(body);

        if (value_tracked) {
            alignment_info.pop(op->name);
        }
        if (wrapper_tracked) {
            alignment_info.pop(wrapper_name);
        }

        scope.pop(op->name);

        if (wrapper_value.defined()) {
            return new T(wrapper_name, wrapper_value, new T(op->name, value, body));
        } else if (body.same_as(op->body) && value.same_as(op->value)) {
            return op;
        } else {
            return new T(op->name, value, body);
        }        
    }


    void visit(const Let *op) {
        expr = simplify_let<Let, Expr>(op, scope, this);
    }

    void visit(const LetStmt *op) {
        stmt = simplify_let<LetStmt, Stmt>(op, scope, this);
    }

    void visit(const PrintStmt *op) {
        IRMutator::visit(op);
    }

    void visit(const AssertStmt *op) {
        IRMutator::visit(op);
    }

    void visit(const Pipeline *op) {
        IRMutator::visit(op);
    }

    void visit(const For *op) {
        IRMutator::visit(op);
    }

    void visit(const Store *op) {
        IRMutator::visit(op);
    }

    void visit(const Provide *op) {
        IRMutator::visit(op);
    }

    void visit(const Allocate *op) {
        IRMutator::visit(op);
    }

    void visit(const Realize *op) {
        IRMutator::visit(op);
    }

    void visit(const Block *op) {        
        IRMutator::visit(op);
    }    
};

Expr simplify(Expr e) {
    return Simplify().mutate(e);
}

Stmt simplify(Stmt s) {
    return Simplify().mutate(s);
}

void check(Expr a, Expr b) {
    Expr simpler = simplify(a);
    if (!equal(simpler, b)) {
        std::cout << std::endl << "Simplification failure: " << std::endl;
        std::cout << "Input: " << a << std::endl;
        std::cout << "Output: " << simpler << std::endl;
        std::cout << "Expected output: " << b << std::endl;
        assert(false);
    }
}
        
void simplify_test() {
    Expr x = Var("x"), y = Var("y"), z = Var("z");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);

    check(new Cast(Int(32), new Cast(Int(32), x)), x);
    check(new Cast(Float(32), 3), 3.0f);
    check(new Cast(Int(32), 5.0f), 5);

    check(new Cast(Int(32), new Cast(Int(8), 3)), 3);
    check(new Cast(Int(32), new Cast(Int(8), 1232)), -48);

    check(3 + x, x + 3);
    check(Expr(3) + Expr(8), 11);
    check(Expr(3.25f) + Expr(7.75f), 11.0f);
    check(x + 0, x);
    check(0 + x, x);
    check(Expr(new Ramp(x, 2, 3)) + Expr(new Ramp(y, 4, 3)), new Ramp(x+y, 6, 3));
    check(Expr(new Broadcast(4.0f, 5)) + Expr(new Ramp(3.25f, 4.5f, 5)), new Ramp(7.25f, 4.5f, 5));
    check(Expr(new Ramp(3.25f, 4.5f, 5)) + Expr(new Broadcast(4.0f, 5)), new Ramp(7.25f, 4.5f, 5));
    check(Expr(new Broadcast(3, 3)) + Expr(new Broadcast(1, 3)), new Broadcast(4, 3));
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
    check(Expr(new Ramp(x, 2, 3)) - Expr(new Ramp(y, 4, 3)), new Ramp(x-y, -2, 3));
    check(Expr(new Broadcast(4.0f, 5)) - Expr(new Ramp(3.25f, 4.5f, 5)), new Ramp(0.75f, -4.5f, 5));
    check(Expr(new Ramp(3.25f, 4.5f, 5)) - Expr(new Broadcast(4.0f, 5)), new Ramp(-0.75f, 4.5f, 5));
    check(Expr(new Broadcast(3, 3)) - Expr(new Broadcast(1, 3)), new Broadcast(2, 3));
    check((x + y) - x, y);
    check((x + y) - y, x);
    check(x - (x + y), 0 - y);
    check(x - (y + x), 0 - y);
    check((x + 3) - 2, x + 1);
    check((x + 3) - y, (x - y) + 3);
    check((x - 3) - y, (x - y) + (-3));
    check(x - (y - 2), (x - y) + 2);
    check(3 - (y - 2), 5 - y);
    check(x*y - x*z, x*(y-z));
    check(x*y - z*x, x*(y-z));
    check(y*x - x*z, x*(y-z));
    check(y*x - z*x, x*(y-z));

    check(x*0, 0);
    check(0*x, 0);
    check(x*1, x);
    check(1*x, x);
    check(Expr(2.0f)*4.0f, 8.0f);
    check(Expr(2)*4, 8);
    check((3*x)*4, x*12);
    check(4*(3+x), x*4 + 12);
    check(Expr(new Broadcast(4.0f, 5)) * Expr(new Ramp(3.0f, 4.0f, 5)), new Ramp(12.0f, 16.0f, 5));
    check(Expr(new Ramp(3.0f, 4.0f, 5)) * Expr(new Broadcast(2.0f, 5)), new Ramp(6.0f, 8.0f, 5));
    check(Expr(new Broadcast(3, 3)) * Expr(new Broadcast(2, 3)), new Broadcast(6, 3));

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
    check(xf / 4.0f, xf * 0.25f);
    check(Expr(new Broadcast(y, 4)) / Expr(new Broadcast(x, 4)), 
          Expr(new Broadcast(y/x, 4)));
    check(Expr(new Ramp(x, 4, 4)) / 2, new Ramp(x/2, 2, 4));

    check(Expr(7) % 2, 1);
    check(Expr(7.25f) % 2.0f, 1.25f);
    check(Expr(-7.25f) % 2.0f, 0.75f);
    check(Expr(new Broadcast(x, 4)) % Expr(new Broadcast(y, 4)), 
          Expr(new Broadcast(x % y, 4)));
    check((x*8) % 4, 0);
    check((x*8 + y) % 4, y);
    check((y + x*8) % 4, y);

    check(new Min(7, 3), 3);
    check(new Min(4.25f, 1.25f), 1.25f);
    check(new Min(new Broadcast(x, 4), new Broadcast(y, 4)), 
          new Broadcast(new Min(x, y), 4));
    check(new Min(x, x+3), x);
    check(new Min(x+4, x), x);
    check(new Min(x-1, x+2), x+(-1));
    check(new Min(7, new Min(x, 3)), new Min(x, 3));
    check(new Min(new Min(x, y), x), new Min(x, y));
    check(new Min(new Min(x, y), y), new Min(x, y));
    check(new Min(x, new Min(x, y)), new Min(x, y));
    check(new Min(y, new Min(x, y)), new Min(x, y));

    check(new Max(7, 3), 7);
    check(new Max(4.25f, 1.25f), 4.25f);
    check(new Max(new Broadcast(x, 4), new Broadcast(y, 4)), 
          new Broadcast(new Max(x, y), 4));
    check(new Max(x, x+3), x+3);
    check(new Max(x+4, x), x+4);
    check(new Max(x-1, x+2), x+2);
    check(new Max(7, new Max(x, 3)), new Max(x, 7));
    check(new Max(new Max(x, y), x), new Max(x, y));
    check(new Max(new Max(x, y), y), new Max(x, y));
    check(new Max(x, new Max(x, y)), new Max(x, y));
    check(new Max(y, new Max(x, y)), new Max(x, y));

    Expr t = const_true(), f = const_false();
    check(x == x, t);
    check(x == (x+1), f);
    check(x-2 == y+3, x == y+5);
    check(x+y == y+z, x == z);
    check(y+x == y+z, x == z);
    check(x+y == z+y, x == z);
    check(y+x == z+y, x == z);
    check((y+x)*17 == (z+y)*17, x == z);
    check(x*0 == y*0, t);
    check(x == x+y, y == 0);
    check(x+y == x, y == 0);

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

    check(!f, t);
    check(!t, f);
    check(!(x < y), y <= x);
    check(!(x > y), x <= y);
    check(!(x >= y), x < y);
    check(!(x <= y), y < x);
    check(!(x == y), x != y);
    check(!(x != y), x == y);
    check(!(!(x == 0)), x == 0);
    check(!Expr(new Broadcast(x > y, 4)), 
          new Broadcast(x <= y, 4));

    check(t && (x < 0), x < 0);
    check(f && (x < 0), f);
    check(t || (x < 0), t);
    check(f || (x < 0), x < 0);

    Expr vec = new Variable(Int(32, 4), "vec");
    // Check constants get pushed inwards
    check(new Let("x", 3, x+4), new Let("x", 3, 7));

    // Check ramps in lets get pushed inwards
    check(new Let("vec", new Ramp(x*2+7, 3, 4), vec + Expr(new Broadcast(2, 4))), 
          new Let("vec.base", x*2+7, 
                  new Let("vec", new Ramp(x*2+7, 3, 4), 
                          new Ramp(Expr(new Variable(Int(32), "vec.base")) + 2, 3, 4))));

    // Check broadcasts in lets get pushed inwards
    check(new Let("vec", new Broadcast(x, 4), vec + Expr(new Broadcast(2, 4))),
          new Let("vec.value", x, 
                  new Let("vec", new Broadcast(x, 4), 
                          new Broadcast(Expr(new Variable(Int(32), "vec.value")) + 2, 4))));
    // Check values don't jump inside lets that share the same name
    check(new Let("x", 3, Expr(new Let("x", y, x+4)) + x), 
          new Let("x", 3, Expr(new Let("x", y, y+4)) + 3));

    check(new Let("x", y, Expr(new Let("x", y*17, x+4)) + x), 
          new Let("x", y, Expr(new Let("x", y*17, x+4)) + y));

    std::cout << "Simplify test passed" << std::endl;
}
}
}
