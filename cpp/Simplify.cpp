#include "Simplify.h"

namespace HalideInternal {

    void Simplify::visit(const IntImm *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const FloatImm *op) {
        IRMutator::visit(op);
    }

    void Simplify::visit(const Cast *op) {
        IRMutator::visit(op);
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
        IRMutator::visit(op);
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
