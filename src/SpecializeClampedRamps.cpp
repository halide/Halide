#include "SpecializeClampedRamps.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

namespace {
class PredicateFinder : public IRMutator {
public:
    Expr min_predicate, max_predicate;
    PredicateFinder() : min_predicate(const_true()), max_predicate(const_true()) {}

private:
    using IRVisitor::visit;

    void visit(const Min *op) {
        Expr a = simplify(mutate(op->a));
        Expr b = simplify(mutate(op->b));

        const Ramp *ra = a.as<Ramp>();
        const Ramp *rb = b.as<Ramp>();
        const Broadcast *ba = a.as<Broadcast>();
        const Broadcast *bb = b.as<Broadcast>();

        if (rb && ba) {
            std::swap(a, b);
            std::swap(ra, rb);
            std::swap(ba, bb);
        }

        if (ra && bb) {
            Expr max_a = ra->base + ra->stride * (ra->width - 1);
            Expr min_b = bb->value;
            min_predicate = min_predicate && (max_a <= min_b);
            expr = a;
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Min::make(a, b);
        }
    }

    void visit(const Max *op) {
        Expr a = simplify(mutate(op->a));
        Expr b = simplify(mutate(op->b));

        const Ramp *ra = a.as<Ramp>();
        const Ramp *rb = b.as<Ramp>();
        const Broadcast *ba = a.as<Broadcast>();
        const Broadcast *bb = b.as<Broadcast>();

        if (rb && ba) {
            std::swap(a, b);
            std::swap(ra, rb);
            std::swap(ba, bb);
        }

        if (ra && bb) {
            Expr min_a = ra->base;
            Expr max_b = bb->value;
            max_predicate = max_predicate && (min_a >= max_b);
            expr = a;
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = Max::make(a, b);
        }
    }

    void visit(const Let *op) {
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);

        if (value.same_as(op->value) && body.same_as(op->body)) {
            expr = op;
        } else {
            expr = Let::make(op->name, value, body);
        }

        min_predicate = substitute(op->name, value, min_predicate);
        max_predicate = substitute(op->name, value, max_predicate);
    }
};

class SpecializeClampedRamps : public IRMutator {
    using IRMutator::visit;

    void visit(const Store *op) {
        PredicateFinder p;
        Stmt simpler_store = p.mutate(op);
        if (simpler_store.same_as(op)) {
            stmt = op;
        } else {
            Expr predicate = simplify(p.min_predicate && p.max_predicate);
            stmt = IfThenElse::make(predicate, simpler_store, op);
        }
    }

    void visit(const LetStmt *op) {
        PredicateFinder p;
        Stmt body = mutate(op->body);
        Expr simpler_value = p.mutate(op->value);
        if (body.same_as(op->body) && simpler_value.same_as(op->value)) {
            stmt = op;
        } else if (simpler_value.same_as(op->value)) {
            stmt = LetStmt::make(op->name, op->value, body);
        } else {
            Stmt simpler_let = LetStmt::make(op->name, simpler_value, body);
            Expr predicate = simplify(p.min_predicate && p.max_predicate);
            stmt = IfThenElse::make(predicate, simpler_let, op);
        }
    }
};
}

Stmt specialize_clamped_ramps(Stmt s) {
    return SpecializeClampedRamps().mutate(s);
}

}
}
