#include "Derivative.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "Scope.h"
#include "IROperator.h"

namespace Halide { 
namespace Internal {

using std::string;

class FiniteDifference : public IRMutator {
    Scope<Expr> scope;
    string var;

    Expr brute_force(Expr e) {
        return substitute(var, (new Variable(Int(32), var)) + 1, e) - e;
    }

    using IRMutator::visit;

    void visit(const IntImm *) {
        expr = 0;
    }

    void visit(const FloatImm *) {
        expr = 0.0f;
    }

    void visit(const Cast *op) {
        expr = brute_force(op);
    }

    void visit(const Variable *op) {
        if (op->name == var) {
            expr = make_one(op->type);
        } else if (scope.contains(op->name)) {
            expr = scope.get(op->name);
        } else {
            expr = make_zero(op->type);
        }
    }

    void visit(const Add *op) {
        expr = mutate(op->a) + mutate(op->b);
    }

    void visit(const Sub *op) {
        expr = mutate(op->a) - mutate(op->b);
    }

    void visit(const Mul *op) {
        Expr da = mutate(op->a), db = mutate(op->b);
        expr = op->a * db + da * op->b + da * db;
    }

    void visit(const Div *op) {
        expr = brute_force(op);
    }

    void visit(const Mod *op) {
        expr = brute_force(op);
    }

    void visit(const Min *op) {
        expr = select(op->a < op->b, mutate(op->a), mutate(op->b));
    }

    void visit(const Max *op) {
        expr = select(op->a > op->b, mutate(op->a), mutate(op->b));
    }

    void visit(const Select *op) {
        expr = select(op->condition, mutate(op->true_value), mutate(op->false_value));
    }

    void visit(const Load *op) {
        expr = brute_force(op);
    }

    void visit(const Call *op) {
        expr = brute_force(op);
    }

    void visit(const Let *op) {
        scope.push(op->name, mutate(op->value));
        expr = mutate(op->body);
        scope.pop(op->name);
    }
public:
    FiniteDifference(string v) : var(v) {}
};

Expr finite_difference(Expr expr, const string &var) {
    return FiniteDifference(var).mutate(expr);
}
  
}
}
