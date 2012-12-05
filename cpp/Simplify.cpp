#include "Simplify.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRPrinter.h"
#include <iostream>

namespace HalideInternal {

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

    void Simplify::visit(const Var *op) {
        // If we marked this var as trivial in the scope, then we
        // should just replace it with its value
        if (scope.contains(op->name)) {
            expr = scope.get(op->name);
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
            expr = new IntImm(ia + ib);
            // const int + const int
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb)) {
            // const float + const float
            expr = new FloatImm(fa + fb);
        } else if (const_int(a, &ia) && ia == 0) {
            // 0 + b
            expr = b;
        } else if (const_int(b, &ib) && ib == 0) {            
            // a + 0
            expr = a;
        } else if (ramp_a && ramp_b) {
            // Ramp + Ramp
            expr = new Ramp(mutate(ramp_a->base + ramp_b->base),
                            mutate(ramp_a->stride + ramp_b->stride), ramp_a->width);
        } else if (ramp_a && broadcast_b) {
            // Ramp + Broadcast
            expr = new Ramp(mutate(ramp_a->base + broadcast_b->value), 
                            ramp_a->stride, ramp_a->width);
        } else if (broadcast_a && ramp_b) {
            // Broadcast + Ramp
            expr = new Ramp(mutate(broadcast_a->value + ramp_b->base), 
                            ramp_b->stride, ramp_b->width);
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
        IRMutator::visit(op);
    }

    void Simplify::visit(const Mul *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Div *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Mod *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Min *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Max *op) {
        IRMutator::visit(op);
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

    void Simplify::visit(const Let *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const LetStmt *op) {
        // If the value is trivial, make a note of it in the scope so
        // we can subs it in later
        Expr value = mutate(op->value);
        bool trivial = false;
        const Ramp *ramp;
        const Broadcast *broadcast;
        if (value.as<IntImm>() || value.as<FloatImm>()) {
            trivial = true;
        } else if ((ramp = value.as<Ramp>()) &&
                   ramp->stride.as<IntImm>() && 
                   ramp->base.as<IntImm>()) {
            trivial = true;
        } else if ((broadcast = value.as<Broadcast>()) &&
                   broadcast->value.as<IntImm>()) {
            trivial = true;
        }

        if (trivial) {
            scope.push(op->name, value);
        }

        Stmt body = mutate(op->body);

        if (trivial) {
            scope.pop(op->name);
        }

        if (body.same_as(op->body) && value.same_as(op->value)) {
            stmt = op;
        } else {
            stmt = new LetStmt(op->name, value, body);
        }

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
        Expr x = new Var(Int(32), "x");
        Expr y = new Var(Int(32), "y");
        Expr z = new Var(Int(32), "z");

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
    }
};
