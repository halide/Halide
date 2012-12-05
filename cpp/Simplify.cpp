#include "Simplify.h"
#include "IROperator.h"

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

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
       
        if (const_int(a, &ia) &&
            const_int(b, &ib)) {
            expr = new IntImm(ia + ib);
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb)) {
            expr = new FloatImm(fa + fb);
        } else if (const_int(a, &ia) && ia == 0) {
            expr = b;
        } else if (const_int(b, &ib) && ib == 0) {
            expr = a;
        } else if (ramp_a && ramp_b) {
            expr = new Ramp(mutate(ramp_a->base + ramp_b->base),
                            mutate(ramp_a->stride + ramp_b->stride), ramp_a->width);
        } else if (ramp_a && broadcast_b) {
            expr = new Ramp(mutate(ramp_a->base + broadcast_b->value), 
                            ramp_a->stride, ramp_a->width);
        } else if (broadcast_a && ramp_b) {
            expr = new Ramp(mutate(broadcast_a->value + ramp_b->base), 
                            ramp_b->stride, ramp_b->width);
        } else if (broadcast_a && broadcast_b) {
            expr = new Broadcast(mutate(broadcast_a->value + broadcast_b->value),
                                 broadcast_a->width);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
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
};
