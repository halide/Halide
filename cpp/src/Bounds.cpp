#include "Bounds.h"
#include "IRVisitor.h"
#include "IR.h"
#include "IROperator.h"
#include "IREquality.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "Util.h"
#include "Var.h"
#include "Log.h"
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
                max = cast(t, (1 << t.bits) - 1);
                min = cast(t, 0);
            } else {
                max = Expr();
                min = Expr();
            }
        } else if (t.is_int()) {
            if (t.bits <= 16) {
                max = cast(t, (1 << (t.bits-1)) - 1);
                min = cast(t, -(1 << (t.bits-1)));
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
        op->value.accept(this);
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
        // Special-case optimizations to generate less work for the constant-folder
        if (is_const(op->a)) {
            op->b.accept(this);
            if (is_negative_const(op->a)) std::swap(min, max);
            if (min.defined()) min *= op->a;
            if (max.defined()) max *= op->a;
        } else if (is_const(op->b)) {
            op->a.accept(this);
            if (is_negative_const(op->b)) std::swap(min, max);
            if (min.defined()) min *= op->b;
            if (max.defined()) max *= op->b;            
        } else {

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
    }

    void visit(const Div *op) {
        op->a.accept(this);

        if (!min.defined() || !max.defined()) {
            min = Expr(); max = Expr(); return;
        }

        if (is_const(op->b)) {
            if (is_negative_const(op->b)) std::swap(min, max);                
            if (min.defined()) min /= op->b;
            if (max.defined()) max /= op->b;
        } else {

            Expr min_a = min, max_a = max;
            op->b.accept(this);
            
            // if we can't statically prove that the divisor can't span zero, then we're unbounded
            Expr min_is_positive = simplify(min > make_zero(min.type()));
            Expr max_is_negative = simplify(max < make_zero(max.type()));
            if (!equal(min, max) && !equal(min_is_positive, const_true()) && !equal(max_is_negative, const_true())) {
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
    }

    void visit(const Mod *op) {
        op->b.accept(this);
        if (!min.defined() || !max.defined()) return;
        min = make_zero(op->type);
        if (!max.type().is_float()) {
            max = max - 1;
        }
    }

    void visit(const Min *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        log(3) << "Bounds of " << Expr(op) << "\n";

        if (min_a.defined() && min_b.defined()) {
            min = new Min(min_a, min_b);
        } else {
            min = Expr();
        }

        if (max_a.defined() && max_b.defined()) {
            max = new Min(max_a, max_b);
        } else {
            max = max_a.defined() ? max_a : max_b;
        }

        log(3) << min << ", " << max << "\n";
    }


    void visit(const Max *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        log(3) << "Bounds of " << Expr(op) << "\n";

        if (min_a.defined() && min_b.defined()) {
            min = new Max(min_a, min_b);
        } else {
            min = min_a.defined() ? min_a : min_b;
        }

        if (max_a.defined() && max_b.defined()) {
            max = new Max(max_a, max_a);
        } else {
            max = Expr();
        }

        log(3) << min << ", " << max << "\n";
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
    Expr max, min;
    log(3) << "Range union of " << a.first << ", " << a.second << ",  " << b.first << ", " << b.second << "\n";
    if (a.second.defined() && b.second.defined()) max = new Max(a.second, b.second);
    if (a.first.defined() && b.first.defined()) min = new Min(a.first, b.first);
    return make_pair(min, max);
}

vector<pair<Expr, Expr> > region_union(const vector<pair<Expr, Expr> > &a, 
                                       const vector<pair<Expr, Expr> > &b) {
    assert(a.size() == b.size() && "Mismatched dimensionality in region union");
    vector<pair<Expr, Expr> > result;
    for (size_t i = 0; i < a.size(); i++) {
        Expr min = new Min(a[i].first, b[i].first);
        Expr max_a = a[i].first + a[i].second - 1;
        Expr max_b = b[i].first + b[i].second - 1;
        Expr max = new Max(max_a, max_b);
        Expr extent = (max + 1) - min;
        result.push_back(make_pair(simplify(min), simplify(extent)));
    }
    return result;
}

class RegionTouched : public IRVisitor {
public:
    Scope<pair<Expr, Expr> > scope;
    map<string, vector<pair<Expr, Expr> > > regions; // Min, Max per dimension
    bool consider_calls;
    bool consider_provides;
    Scope<int> inside_update;
private:
    void visit(const LetStmt *op) {
        op->value.accept(this);
        pair<Expr, Expr> value_bounds = bounds_of_expr_in_scope(op->value, scope);
        scope.push(op->name, value_bounds);
        op->body.accept(this);
        scope.pop(op->name);
    }
    
    void visit(const Let *op) {
        op->value.accept(this);
        pair<Expr, Expr> value_bounds = bounds_of_expr_in_scope(op->value, scope);
        scope.push(op->name, value_bounds);
        op->body.accept(this);        
        scope.pop(op->name);
    }

    void visit(const For *op) {
        op->min.accept(this);
        op->extent.accept(this);
        pair<Expr, Expr> min_bounds = bounds_of_expr_in_scope(op->min, scope);
        pair<Expr, Expr> extent_bounds = bounds_of_expr_in_scope(op->extent, scope);
        Expr min = min_bounds.first;
        Expr max = (min_bounds.second + extent_bounds.second) - 1;
        scope.push(op->name, make_pair(min, max));
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const Call *op) {        
        IRVisitor::visit(op);
        // Ignore calls to a function from within it's own update step
        // (i.e. recursive calls from a function to itself). Including
        // these gives recursive definitions of the bounds (f requires
        // as much as f requires!). We make sure we cover the bounds
        // required by the update step of a reduction elsewhere (in
        // InjectRealization in Lower.cpp)
        if (consider_calls && !inside_update.contains(op->name)) {
            log(3) << "Found call to " << op->name << ": " << Expr(op) << "\n";
            vector<pair<Expr, Expr> > &region = regions[op->name];
            for (size_t i = 0; i < op->args.size(); i++) {
                pair<Expr, Expr> bounds = bounds_of_expr_in_scope(op->args[i], scope);
                log(3) << "Bounds of call to " << op->name << " in dimension " << i << ": " 
                       << bounds.first << ", " << bounds.second << "\n";
                if (region.size() > i) {
                    region[i] = range_union(region[i], bounds);
                } else {
                    region.push_back(bounds);
                }
            }
        }
    }

    void visit(const Provide *op) {        
        IRVisitor::visit(op);
        if (consider_provides) {
            vector<pair<Expr, Expr> > &region = regions[op->name];
            for (size_t i = 0; i < op->args.size(); i++) {
                pair<Expr, Expr> bounds = bounds_of_expr_in_scope(op->args[i], scope);
                if (region.size() > i) {
                    region[i] = range_union(region[i], bounds);
                } else {
                    region.push_back(bounds);
                }
            }
        }
    }

    void visit(const Pipeline *op) {
        op->produce.accept(this);
        if (op->update.defined()) {
            inside_update.push(op->name, 0);
            op->update.accept(this);
            inside_update.pop(op->name);
        }
        op->consume.accept(this);
    }
};

map<string, vector<pair<Expr, Expr> > > compute_regions_touched(Stmt s, const Scope<pair<Expr, Expr> > &scope, 
                                                                bool consider_calls, bool consider_provides) {
    RegionTouched r;
    r.consider_calls = consider_calls;
    r.consider_provides = consider_provides;
    r.scope = scope;
    s.accept(&r);
    // Convert from (min, max) to (min, extent)
    for (map<string, vector<pair<Expr, Expr> > >::iterator iter = r.regions.begin(); 
         iter != r.regions.end(); iter++) {
        for (size_t i = 0; i < iter->second.size(); i++) {
            /*
            if (!iter->second[i].first.defined()) {
                std::cerr << "Usage of " << iter->first << " is unbounded below in the following statement: " << s << std::endl;
                assert(false);
            }
            if (!iter->second[i].second.defined()) {
                std::cerr << "Usage of " << iter->first << " is unbounded above in the following statement: " << s << std::endl;
                assert(false);
            }
            */
            if (!iter->second[i].first.defined() || !iter->second[i].second.defined()) {
                iter->second[i] = make_pair(Expr(), Expr());
            } else {
                // the max is likely to be of the form foo-1
                iter->second[i].first = simplify(iter->second[i].first);
                iter->second[i].second = simplify((iter->second[i].second + 1) - iter->second[i].first);
            }
        }
    }
    return r.regions;
}

map<string, vector<pair<Expr, Expr> > > regions_provided(Stmt s, const Scope<pair<Expr, Expr> > &scope) {
    return compute_regions_touched(s, scope, false, true);
}

map<string, vector<pair<Expr, Expr> > > regions_required(Stmt s, const Scope<pair<Expr, Expr> > &scope) {
    return compute_regions_touched(s, scope, true, false);
}

map<string, vector<pair<Expr, Expr> > > regions_touched(Stmt s, const Scope<pair<Expr, Expr> > &scope) {
    return compute_regions_touched(s, scope, true, true);
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
    Var x("x"), y("y");
    scope.push("x", make_pair(Expr(0), Expr(10)));

    check(scope, x, 0, 10);
    check(scope, x+1, 1, 11);
    check(scope, (x+1)*2, 2, 22);
    check(scope, x*x, 0, 100);
    check(scope, 5-x, -5, 5);
    check(scope, x*(5-x), -50, 50); // We don't expect bounds analysis to understand correlated terms
    check(scope, new Select(x < 4, x, x+100), 0, 110);
    check(scope, x+y, y, y+10);
    check(scope, x*y, new Min(y*10, 0), new Max(y*10, 0));
    check(scope, x/(x+y), Expr(), Expr());
    check(scope, 11/(x+1), 1, 11);
    check(scope, new Load(Int(8), "buf", x, Buffer(), Parameter()), cast(Int(8), -128), cast(Int(8), 127));
    check(scope, y + (new Let("y", x+3, y - x + 10)), y + 3, y + 23); // Once again, we don't know that y is correlated with x
    check(scope, clamp(1/(x-2), x-10, x+10), -10, 20);

    vector<Expr> input_site_1 = vec(2*x);
    vector<Expr> input_site_2 = vec(2*x+1);
    vector<Expr> output_site = vec(x+1);

    Stmt loop = new For("x", 3, x, For::Serial, 
                        new Provide("output", 
                                    new Add(
                                        new Call(Int(32), "input", input_site_1),
                                        new Call(Int(32), "input", input_site_2)),
                                    output_site));

    map<string, vector<pair<Expr, Expr> > > r;
    r = regions_required(loop, scope);
    assert(r.find("output") == r.end());
    assert(r.find("input") != r.end());
    assert(equal(r["input"][0].first, 6));
    assert(equal(r["input"][0].second, 20));
    r = regions_provided(loop, scope);
    assert(r.find("output") != r.end());
    assert(equal(r["output"][0].first, 4));
    assert(equal(r["output"][0].second, 10));

    vector<pair<Expr, Expr> > r2 = vec(make_pair(Expr(5), Expr(15)));
    r2 = region_union(r["output"], r2);
    assert(equal(r2[0].first, 4));
    assert(equal(r2[0].second, 16));

    std::cout << "Bounds test passed" << std::endl;
}

}
}
