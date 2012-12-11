#include "Bounds.h"
#include "IRVisitor.h"
#include "IR.h"
#include "IROperator.h"
#include "IREquality.h"
#include "Simplify.h"

// This file is largely a port of src/bounds.ml

namespace Halide { 
namespace Internal {

class Bounds : public IRVisitor {
    Expr min, max;
    Scope<pair<Expr, Expr> > scope;
    void visit(const IntImm *op) {
        min = op;
        max = op;
    }
    
    void visit(const FloatImm *op) {
        min = op;
        max = op;
    }

    void visit(const Cast *op) {
        op->accept(this);
        if (!min.defined()) return;
        min = new Cast(op->type, min);
        max = new Cast(op->type, max);
    }

    void visit(const Variable *op) {
        if (scope.contains(op->name)) {
            pair<Expr, Expr> bounds = scope.get(op->name);
            min = bounds.first;
            max = bounds.second;
        } else {
            min = op;
            max = op;
        }
    }

    void visit(const Add *op) {
        op->a.accept(this);
        if (!min.defined()) return;
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        if (!min.defined()) return;
        min = new Add(min_a, min);
        max = new Add(max_a, max);
    }

    void visit(const Sub *op) {
        op->a.accept(this);
        if (!min.defined()) return;
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        if (!min.defined()) return;
        min = new Sub(min_a, max);
        max = new Sub(max_a, min);
    }

    void visit(const Mul *op) {
        op->a.accept(this);
        if (!min.defined()) return;
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        if (!min.defined()) return;
        Expr a = min_a * min;
        Expr b = min_a * max;
        Expr c = max_a * min;
        Expr d = max_a * max;
        min = new Min(new Min(a, b), new Min(c, d));
        max = new Max(new Max(a, b), new Max(c, d));
    }

    void visit(const Div *op) {
        // if we can't statically prove that the divisor can't be zero, then we're in trouble
        if (!equal(simplify(op->b == make_zero(op->b.type())), const_false())) {
            min = Expr();
            max = Expr();
            return;
        }

        op->a.accept(this);
        if (!min.defined()) return;
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        if (!min.defined()) return;
        Expr a = min_a / min;
        Expr b = min_a / max;
        Expr c = max_a / min;
        Expr d = max_a / max;
        min = new Min(new Min(a, b), new Min(c, d));
        max = new Max(new Max(a, b), new Max(c, d));
    }

    void visit(const Mod *op) {
                
    }

    void visit(const Min *);
    void visit(const Max *);
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GT *);
    void visit(const GE *);
    void visit(const And *);
    void visit(const Or *);
    void visit(const Not *);
    void visit(const Select *);
    void visit(const Load *);
    void visit(const Ramp *);
    void visit(const Broadcast *);
    void visit(const Call *);
    void visit(const Let *);
    void visit(const LetStmt *);
    void visit(const PrintStmt *);
    void visit(const AssertStmt *);
    void visit(const Pipeline *);
    void visit(const For *);
    void visit(const Store *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Realize *);
    void visit(const Block *);
};

bool bounds_of_expr_in_scope(Expr expr, const Scope<pair<Expr, Expr> > &scope, Expr *min, Expr *max) {
    return false;
}

}
}
