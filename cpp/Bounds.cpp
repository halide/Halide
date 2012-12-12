#include "Bounds.h"
#include "IRVisitor.h"
#include "IR.h"
#include "IROperator.h"
#include "IREquality.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "Util.h"
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
        Expr min_b = min, max_b = max;
        min = (max_b.defined() && min_a.defined()) ? new Sub(min_a, max_b) : Expr();
        max = (min_b.defined() && max_a.defined()) ? new Sub(max_a, min_b) : Expr();
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
        Expr min_is_positive = simplify(min > make_zero(min.type()));
        Expr max_is_negative = simplify(max < make_zero(max.type()));
        if (!equal(min_is_positive, const_true()) && !equal(max_is_negative, const_true())) {
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

pair<Expr, Expr> range_union(const pair<Expr, Expr> &a, const pair<Expr, Expr> &b) {
    return make_pair(new Min(a.first, b.first), new Max(a.second, b.second));
}

class RegionRequired : public IRVisitor {
public:
    string func;
    Scope<pair<Expr, Expr> > scope;
    vector<pair<Expr, Expr> > region;
private:
    void visit(const LetStmt *op) {
        pair<Expr, Expr> value_bounds = bounds_of_expr_in_scope(op->value, scope);
        // TODO: What if the value bounds refer to variables that get rebound before this let is used
        scope.push(op->name, value_bounds);
        op->body.accept(this);
        scope.pop(op->name);
    }
    
    void visit(const Let *op) {
        pair<Expr, Expr> value_bounds = bounds_of_expr_in_scope(op->value, scope);
        // TODO: What if the value bounds refer to variables that get rebound before this let is used
        scope.push(op->name, value_bounds);
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const For *op) {
        pair<Expr, Expr> min_bounds = bounds_of_expr_in_scope(op->min, scope);
        pair<Expr, Expr> extent_bounds = bounds_of_expr_in_scope(op->extent, scope);
        Expr min = min_bounds.first;
        Expr max = (min_bounds.second + extent_bounds.second) - 1;
        scope.push(op->name, make_pair(min, max));
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const Call *op) {        
        if (op->name == func) {
            for (size_t i = 0; i < op->args.size(); i++) {
                pair<Expr, Expr> bounds = bounds_of_expr_in_scope(op->args[i], scope);
                if (region.size() > i) {
                    region[i] = range_union(region[i], bounds);
                } else {
                    region.push_back(bounds);
                }
            }
        } else {
            IRVisitor::visit(op);
        }
    }
};

vector<pair<Expr, Expr> > region_provided(string func, Stmt s, const Scope<pair<Expr, Expr> > &scope) {
    vector<pair<Expr, Expr> > r;
    return r;
}

vector<pair<Expr, Expr> > region_required(string func, Stmt s, const Scope<pair<Expr, Expr> > &scope) {
    RegionRequired r;
    r.func = func;
    r.scope = scope;
    s.accept(&r);
    // Convert from (min, max) to min, (extent)
    for (size_t i = 0; i < r.region.size(); i++) {
        // the max is likely to be of the form foo-1
        r.region[i].first = simplify(r.region[i].first);
        r.region[i].second = simplify((r.region[i].second + 1) - r.region[i].first);

    }
    return r.region;
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
    check(scope, x*x, 0, 100);
    check(scope, 5-x, -5, 5);
    check(scope, x*(5-x), -50, 50); // We don't expect bounds analysis to understand correlated terms
    check(scope, new Select(x < 4, x, x+100), 0, 110);
    check(scope, x+y, y, y+10);
    check(scope, x*y, new Min(0, y*10), new Max(0, y*10));
    check(scope, x/y, Expr(), Expr());
    check(scope, 11/(x+1), 1, 11);
    check(scope, new Load(Int(8), "buf", x), -128, 127);
    check(scope, y + (new Let("y", x+3, y - x + 10)), y + 3, y + 23); // Once again, we don't know that y is correlated with x

    vector<Expr> input_site_1 = vec(2*x);
    vector<Expr> input_site_2 = vec(2*x+1);
    vector<Expr> output_site = vec(x+1);

    Stmt loop = new For("x", 3, x, For::Serial, 
                        new Provide("output", 
                                    new Add(
                                        new Call(Int(32), "input", input_site_1, Call::Extern, NULL),
                                        new Call(Int(32), "input", input_site_2, Call::Extern, NULL)),
                                    output_site));

    vector<pair<Expr, Expr> > r;
    r = region_required("output", loop, scope);
    assert(r.empty());
    r = region_required("pants", loop, scope);
    assert(r.empty());
    r = region_required("input", loop, scope);
    assert(equal(r[0].first, 6));
    assert(equal(r[0].second, 20));
    

    std::cout << "Bounds test passed" << std::endl;
}

}
}
