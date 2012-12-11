#include "Bounds.h"
#include "IRVisitor.h"
#include "IR.h"
#include "IROperator.h"
#include "IREquality.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include <iostream>


// This file is largely a port of src/bounds.ml

namespace Halide { 
namespace Internal {

using std::make_pair;

class Bounds : public IRVisitor {
public:
    Expr min, max;
    Scope<pair<Expr, Expr> > scope;

private:
    void bounds_of_type(Type t) {
        if (t.is_uint()) {
            if (t.bits <= 16) {
                max = (1 << t.bits) - 1;
                min = 0;
            } else {
                max = Expr();
                min = Expr();
            }
        } else if (t.is_int()) {
            if (t.bits <= 16) {
                max = (1 << (t.bits-1)) - 1;
                min = -(1 << (t.bits-1));
            }
        } else {
            max = Expr();
            min = Expr();
        }        
    }

    void visit(const IntImm *op) {
        min = op;
        max = op;
    }
    
    void visit(const FloatImm *op) {
        min = op;
        max = op;
    }

    void visit(const Cast *op) {
        // Assume no overflow
        op->accept(this);
        min = min.defined() ? new Cast(op->type, min) : Expr();
        max = max.defined() ? new Cast(op->type, max) : Expr();
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
        Expr min_a = min, max_a = max;
        op->b.accept(this);

        min = (min.defined() && min_a.defined()) ? new Add(min_a, min) : Expr();
        max = (max.defined() && max_a.defined()) ? new Add(max_a, max) : Expr();
    }

    void visit(const Sub *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        min = (min.defined() && min_a.defined()) ? new Sub(min_a, min) : Expr();
        max = (max.defined() && max_a.defined()) ? new Sub(max_a, max) : Expr();
    }

    void visit(const Mul *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        if (!min.defined() || !max.defined()) {
            min = Expr(); max = Expr(); return;
        }

        op->b.accept(this);
        if (!min.defined() || !max.defined()) {
            min = Expr(); max = Expr(); return;
        }

        Expr a = min_a * min;
        Expr b = min_a * max;
        Expr c = max_a * min;
        Expr d = max_a * max;

        min = new Min(new Min(a, b), new Min(c, d));
        max = new Max(new Max(a, b), new Max(c, d));
    }

    void visit(const Div *op) {
        op->a.accept(this);

        if (!min.defined() || !max.defined()) {
            min = Expr(); max = Expr(); return;
        }

        Expr min_a = min, max_a = max;
        op->b.accept(this);

        // if we can't statically prove that the divisor can't span zero, then we're unbounded
        if (!equal(simplify(min > make_zero(min.type())), const_true()) ||
            !equal(simplify(max < make_zero(min.type())), const_true())) {
            min = Expr();
            max = Expr();
            return;
        }

        if (!min.defined() || !max.defined()) {
            min = Expr(); max = Expr(); return;
        }

        Expr a = min_a / min;
        Expr b = min_a / max;
        Expr c = max_a / min;
        Expr d = max_a / max;
        min = new Min(new Min(a, b), new Min(c, d));
        max = new Max(new Max(a, b), new Max(c, d));
    }

    void visit(const Mod *op) {
        op->b.accept(this);
        if (!min.defined() || !max.defined()) return;
        min = make_zero(op->type);
    }

    void visit(const Min *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);

        if (min.defined() && min_a.defined()) {
            min = new Min(min, min_a);
        } else {
            min = Expr();
        }

        if (max.defined() && max_a.defined()) {
            max = new Min(max, max_a);
        } else {
            max = max.defined() ? max : max_a;
        }
    }


    void visit(const Max *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);

        if (min.defined() && min_a.defined()) {
            min = new Max(min, min_a);
        } else {
            min = min.defined() ? min : min_a;
        }

        if (max.defined() && max_a.defined()) {
            max = new Max(max, max_a);
        } else {
            max = Expr();
        }
    }

    void visit(const EQ *) {
        assert(false && "Bounds of boolean");
    }

    void visit(const NE *) {
        assert(false && "Bounds of boolean");
    }

    void visit(const LT *) {
        assert(false && "Bounds of boolean");
    }

    void visit(const LE *) {
        assert(false && "Bounds of boolean");
    }

    void visit(const GT *) {
        assert(false && "Bounds of boolean");
    }

    void visit(const GE *) {
        assert(false && "Bounds of comparison");
    }

    void visit(const And *) {
        assert(false && "Bounds of comparison");
    }

    void visit(const Or *) {
        assert(false && "Bounds of comparison");
    }

    void visit(const Not *) {
        assert(false && "Bounds of comparison");
    }

    void visit(const Select *op) {
        op->true_value.accept(this);

        if (!min.defined() || !max.defined()) {
            min = Expr(); max = Expr(); return;
        }

        Expr min_a = min, max_a = max;
        op->false_value.accept(this);
        
        min = (min.defined() && min_a.defined()) ? new Min(min, min_a) : Expr();
        max = (max.defined() && max_a.defined()) ? new Max(max, max_a) : Expr();
    }

    void visit(const Load *op) {
        bounds_of_type(op->type);
    }

    void visit(const Ramp *op) {
        assert(false && "Bounds of vector");
    }

    void visit(const Broadcast *) {
        assert(false && "Bounds of vector");
    }

    void visit(const Call *op) {
        bounds_of_type(op->type);
    }

    void visit(const Let *op) {
        op->value.accept(this);
        scope.push(op->name, make_pair(min, max));
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const LetStmt *) {
        assert(false && "Bounds of statement");
    }

    void visit(const PrintStmt *) {
        assert(false && "Bounds of statement");
    }

    void visit(const AssertStmt *) {
        assert(false && "Bounds of statement");
    }

    void visit(const Pipeline *) {
        assert(false && "Bounds of statement");
    }

    void visit(const For *) {
        assert(false && "Bounds of statement");
    }

    void visit(const Store *) {
        assert(false && "Bounds of statement");
    }

    void visit(const Provide *) {
        assert(false && "Bounds of statement");
    }

    void visit(const Allocate *) {
        assert(false && "Bounds of statement");
    }

    void visit(const Realize *) {
        assert(false && "Bounds of statement");
    }

    void visit(const Block *) {
        assert(false && "Bounds of statement");
    }
};

pair<Expr, Expr> bounds_of_expr_in_scope(Expr expr, const Scope<pair<Expr, Expr> > &scope) {
    Bounds b;
    b.scope = scope;
    expr.accept(&b);
    return make_pair(b.min, b.max);
}

void check(const Scope<pair<Expr, Expr> > &scope, Expr e, Expr correct_min, Expr correct_max) {
    pair<Expr, Expr> result = bounds_of_expr_in_scope(e, scope);
    if (result.first.defined()) result.first = simplify(result.first);
    if (result.second.defined()) result.second = simplify(result.second);
    bool success = true;
    if (!equal(result.first, correct_min)) {
        std::cout << "Incorrect min: " << result.first << std::endl
                  << "Should have been: " << correct_min << std::endl;
        success = false;
    }
    if (!equal(result.second, correct_max)) {
        std::cout << "Incorrect max: " << result.second << std::endl
                  << "Should have been: " << correct_max << std::endl;
        success = false;
    }
    assert(success && "Bounds test failed");
}

void bounds_test() {
    Scope<pair<Expr, Expr> > scope;
    Expr x = new Variable(Int(32), "x");
    Expr y = new Variable(Int(32), "y");
    scope.push("x", make_pair(Expr(0), Expr(10)));

    check(scope, x, 0, 10);
    check(scope, x+1, 1, 11);
    check(scope, (x+1)*2, 2, 22);
}

}
}
