#include "Simplify.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRPrinter.h"
#include <iostream>

namespace Halide { namespace Internal {

    void Simplify::visit(const IntImm *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const FloatImm *op) {
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

    bool is_const(Expr e) {
        if (e.as<IntImm>()) return true;
        if (e.as<FloatImm>()) return true;
        if (const Ramp *r = e.as<Ramp>()) {
            return is_const(r->base) && is_const(r->stride);
        }
        if (const Broadcast *b = e.as<Broadcast>()) {
            return is_const(b->value);
        }
        return false;

    }

    bool is_zero(Expr e) {
        int i;
        float f;
        if (const_int(e, &i)) return i == 0;
        if (const_float(e, &f)) return f == 0;
        if (const Cast *c = e.as<Cast>()) return is_zero(c->value);
        if (const Broadcast *b = e.as<Broadcast>()) return is_zero(b->value);
        return false;
    }

    bool is_one(Expr e) {
        int i;
        float f;
        if (const_int(e, &i)) return i == 1;
        if (const_float(e, &f)) return f == 1.0f;
        if (const Cast *c = e.as<Cast>()) return is_one(c->value);
        if (const Broadcast *b = e.as<Broadcast>()) return is_one(b->value);
        return false;
    }

    Expr make_zero(Type t) {
        if (t == Int(32)) return 0;
        if (t == Float(32)) return 0.0f;
        if (t.is_vector()) {
            return new Broadcast(make_zero(t.element_of()), t.width);
        }
        return new Cast(t, 0);
    }

    Expr make_one(Type t) {
        if (t == Int(32)) return 1;
        if (t == Float(32)) return 1.0f;
        if (t.is_vector()) {
            return new Broadcast(make_one(t.element_of()), t.width);
        }
        return new Cast(t, 1);
    }

    void Simplify::visit(const Cast *op) {
        Expr value = mutate(op->value);        
        float f;
        int i;
        if (value.type() == op->type) {
            expr = value;
        } else if (op->type == Int(32) && const_float(value, &f)) {
            expr = new IntImm((int)f);
        } else if (op->type == Float(32) && const_int(value, &i)) {
            expr = new FloatImm((float)i);
        } else if (value.same_as(op->value)) {
            expr = op;
        } else {
            expr = new Cast(op->type, value);
        }
    }

    void Simplify::visit(const Variable *op) {
        // If we marked this var as trivial in the scope, then we
        // should just replace it with its value
        if (scope.contains(op->name)) {
            expr = scope.get(op->name);
            if (!expr.defined()) expr = op;
        } else {
            expr = op;
        }
    }

    void Simplify::visit(const Add *op) {
        int ia, ib;
        float fa, fb;

        Expr a = mutate(op->a), b = mutate(op->b);

        // rearrange const + varying to varying + const, to cut down
        // on cases to check
        if (is_const(a)) std::swap(a, b);

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
        } else if (add_a && is_const(add_a->b)) {
            // In ternary expressions, pull constants outside
            if (is_const(b)) expr = mutate(add_a->a + (add_a->b + b));
            else expr = mutate((add_a->a + b) + add_a->b);
        } else if (add_b && is_const(add_b->b)) {
            expr = mutate((a + add_b->a) + add_b->b);
        } else if (sub_a && is_const(sub_a->a) && is_const(b)) {
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

    void Simplify::visit(const Sub *op) {
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
            expr = make_zero(add_b->a.type()) - add_b->a;
        } else if (add_b && equal(add_b->a, a)) {
            expr = make_zero(add_b->a.type()) - add_b->b;

        } else if (add_a && is_const(add_a->b)) {
            // In ternary expressions, pull constants outside
            if (is_const(b)) expr = mutate(add_a->a + (add_a->b - b));
            else expr = mutate((add_a->a - b) + add_a->b);
        } else if (add_b && is_const(add_b->b)) {
            expr = mutate((a - add_b->a) - add_b->b);
        } else if (sub_a && is_const(sub_a->a) && is_const(b)) {
            expr = mutate((sub_a->a - b) - sub_a->b);
        } else if (sub_b && is_const(sub_b->b)) {
            if (is_const(a)) expr = mutate((a + sub_b->b) - sub_b->a);
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

    void Simplify::visit(const Mul *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        if (is_const(a)) std::swap(a, b);

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
        } else if (add_a && is_const(add_a->b) && is_const(b)) {
            expr = mutate(add_a->a * b + add_a->b * b);
        } else if (mul_a && is_const(mul_a->b) && is_const(b)) {
            expr = mutate(mul_a->a * (mul_a->b * b));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Mul(a, b);
        }
    }

    void Simplify::visit(const Div *op) {
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
        } else if (b.type().is_float() && is_const(b)) {
            // Convert const float division to multiplication
            // x / 2 -> x * 0.5
            expr = mutate(a * (make_one(b.type()) / b));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Div(a, b);
        }
    }

    void Simplify::visit(const Mod *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        int ia, ib;
        float fa, fb;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Mul *mul_a = a.as<Mul>();
        const Add *add_a = a.as<Add>();
        const Mul *mul_a_a = add_a ? add_a->a.as<Mul>() : NULL;
        const Mul *mul_a_b = add_a ? add_a->b.as<Mul>() : NULL;
        
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
        } else if (const_int(b, &ib) && false /*TODO: Something about modulus remainder analysis*/) {
            
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Div(a, b);
        }
    }

    void Simplify::visit(const Min *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        int ia, ib;
        float fa, fb;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();

        if (equal(a, b)) {
            expr = a;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            expr = std::min(ia, ib);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            expr = std::min(fa, fb);
        } else if (broadcast_a && broadcast_b) {
            expr = mutate(new Broadcast(new Min(broadcast_a->value, broadcast_b->value), broadcast_a->width));
        } else if (add_a && const_int(add_a->b, &ia) && add_b && const_int(add_b->b, &ib) && equal(add_a->a, add_b->a)) {
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
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Min(a, b);
        }
    }

    void Simplify::visit(const Max *op) {
        Expr a = mutate(op->a), b = mutate(op->b);

        int ia, ib;
        float fa, fb;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();

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
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = new Max(a, b);
        }
    }

    void Simplify::visit(const EQ *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const NE *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const LT *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const LE *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const GT *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const GE *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const And *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Or *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Not *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Select *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Load *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Ramp *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Broadcast *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Call *op) {
        IRMutator::visit(op);
    }

    template<typename T, typename Body> 
    Body simplify_let(const T *op, Scope<Expr> &scope, IRMutator *mutator) {
        // If the value is trivial, make a note of it in the scope so
        // we can subs it in later
        Expr value = mutator->mutate(op->value);
        Body body = op->body;
        const Ramp *ramp = value.as<Ramp>();
        const Broadcast *broadcast = value.as<Broadcast>();        
        if (is_const(value)) {
            // Substitute the value wherever we see it
            scope.push(op->name, value);
        } else if (ramp && is_const(ramp->stride)) {
            // Make a new name to refer to the base instead, and push the ramp inside
            scope.push(op->name, new Ramp(new Variable(ramp->base.type(), op->name + ".base"), ramp->stride, ramp->width));
            body = new T(op->name + ".base", ramp->base, body);
        } else if (broadcast) {
            // Make a new name refer to the scalar version, and push the broadcast inside            
            scope.push(op->name, new Broadcast(new Variable(broadcast->value.type(), op->name + ".value"), broadcast->width));
            body = new T(op->name + ".value", broadcast->value, body);
        } else {
            // Push a empty expr on, to make sure we hide anything
            // else with the same name until this goes out of scope
            scope.push(op->name, Expr());
        }

        body = mutator->mutate(body);

        scope.pop(op->name);

        if (body.same_as(op->body) && value.same_as(op->value)) {
            return op;
        } else {
            return new T(op->name, value, body);
        }        
    }


    void Simplify::visit(const Let *op) {
        expr = simplify_let<Let, Expr>(op, scope, this);
    }

    void Simplify::visit(const LetStmt *op) {
        stmt = simplify_let<LetStmt, Stmt>(op, scope, this);
    }

    void Simplify::visit(const PrintStmt *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const AssertStmt *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Pipeline *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const For *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Store *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Provide *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Allocate *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Realize *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Block *op) {        
        IRMutator::visit(op);
    }    

    void check(Expr a, Expr b) {
        Expr simpler = Simplify().mutate(a);
        if (!equal(simpler, b)) {
            std::cout << std::endl << "Simplification failure: " << std::endl;
            std::cout << "Input: " << a << std::endl;
            std::cout << "Output: " << simpler << std::endl;
            std::cout << "Expected output: " << b << std::endl;
            assert(false);
        }
    }

    void Simplify::test() {
        Expr x = new Variable(Int(32), "x");
        Expr y = new Variable(Int(32), "y");
        Expr z = new Variable(Int(32), "z");
        Expr xf = new Variable(Float(32), "x");
        Expr yf = new Variable(Float(32), "y");

        Simplify s;

        check(new Cast(Int(32), new Cast(Int(32), x)), x);
        check(new Cast(Float(32), 3), 3.0f);
        check(new Cast(Int(32), 5.0f), 5);

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

        check(new Max(7, 3), 7);
        check(new Max(4.25f, 1.25f), 4.25f);
        check(new Max(new Broadcast(x, 4), new Broadcast(y, 4)), 
              new Broadcast(new Max(x, y), 4));
        check(new Max(x, x+3), x+3);
        check(new Max(x+4, x), x+4);
        check(new Max(x-1, x+2), x+2);

        Expr vec = new Variable(Int(32, 4), "vec");
        // Check constants get pushed inwards
        check(new Let("x", 3, x+4), new Let("x", 3, 7));
        // Check ramps in lets get pushed inwards
        check(new Let("vec", new Ramp(x*2, 3, 4), vec + Expr(new Broadcast(2, 4))), 
              new Let("vec", new Ramp(x*2, 3, 4), 
                      new Let("vec.base", x*2, 
                              new Ramp(Expr(new Variable(Int(32), "vec.base")) + 2, 3, 4))));
        // Check broadcasts in lets get pushed inwards
        check(new Let("vec", new Broadcast(x, 4), vec + Expr(new Broadcast(2, 4))),
              new Let("vec", new Broadcast(x, 4), 
                      new Let("vec.value", x, 
                              new Broadcast(Expr(new Variable(Int(32), "vec.value")) + 2, 4))));
        // Check values don't jump inside lets that share the same name
        check(new Let("x", 3, Expr(new Let("x", y, x+4)) + x), 
              new Let("x", 3, Expr(new Let("x", y, x+4)) + 3));


        std::cout << "Simplify test passed" << std::endl;
    }
}}
