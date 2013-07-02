#include "Bounds.h"
#include "IRVisitor.h"
#include "IR.h"
#include "IROperator.h"
#include "IREquality.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "Util.h"
#include "Var.h"
#include "Debug.h"
#include <iostream>


// This file is largely a port of src/bounds.ml

namespace Halide {
namespace Internal {

using std::make_pair;
using std::map;
using std::vector;
using std::string;

class Bounds : public IRVisitor {
public:
    Expr min, max;
    Scope<Interval> scope;

private:
    void bounds_of_type(Type t) {
        if (t.is_uint() && t.bits <= 16) {
            max = cast(t, (1 << t.bits) - 1);
            min = cast(t, 0);
        } else if (t.is_int() && t.bits <= 16) {
            max = cast(t, (1 << (t.bits-1)) - 1);
            min = cast(t, -(1 << (t.bits-1)));
        } else {
            max = Expr();
            min = Expr();
        }
    }

    using IRVisitor::visit;

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
        Expr min_a = min, max_a = max;

        min = min_a.defined() ? Cast::make(op->type, min_a) : Expr();

        if (min_a.same_as(max_a)) {
            max = min;
        } else {
            max = max.defined() ? Cast::make(op->type, max) : Expr();
        }
    }

    void visit(const Variable *op) {
        if (scope.contains(op->name)) {
            Interval bounds = scope.get(op->name);
            min = bounds.min;
            max = bounds.max;
        } else {
            debug(3) << op->name << " not in scope, so leaving it as-is\n";
            min = op;
            max = op;
        }
    }

    void visit(const Add *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        min = (min_b.defined() && min_a.defined()) ? Add::make(min_a, min_b) : Expr();

        if (min_a.same_as(max_a) && min_b.same_as(max_b)) {
            max = min;
        } else {
            max = (max_b.defined() && max_a.defined()) ? Add::make(max_a, max_b) : Expr();
        }
    }

    void visit(const Sub *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        min = (max_b.defined() && min_a.defined()) ? Sub::make(min_a, max_b) : Expr();
        if (min_a.same_as(max_a) && min_b.same_as(max_b)) {
            max = min;
        } else {
            max = (min_b.defined() && max_a.defined()) ? Sub::make(max_a, min_b) : Expr();
        }
    }

    void visit(const Mul *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        if (!min_a.defined() || !max_a.defined()) {
            min = Expr(); max = Expr(); return;
        }

        op->b.accept(this);
        Expr min_b = min, max_b = max;
        if (!min_b.defined() || !max_b.defined()) {
            min = Expr(); max = Expr(); return;
        }

        if (min_a.same_as(max_a) && min_b.same_as(max_b)) {
            // A and B are constant
            min = max = min_a * min_b;
        } else if (min_a.same_as(max_a)) {
            // A is constant
            Expr a = min_a * min_b;
            Expr b = min_a * max_b;
            Expr cmp = min_a > make_zero(min_a.type());
            min = select(cmp, a, b);
            max = select(cmp, b, a);
        } else if (min_b.same_as(max_b)) {
            // B is constant
            Expr a = min_b * min_a;
            Expr b = min_b * max_a;
            Expr cmp = min_b > make_zero(min_b.type());
            min = select(cmp, a, b);
            max = select(cmp, b, a);
        } else {

            Expr a = min_a * min_b;
            Expr b = min_a * max_b;
            Expr c = max_a * min_b;
            Expr d = max_a * max_b;

            min = Min::make(Min::make(a, b), Min::make(c, d));
            max = Max::make(Max::make(a, b), Max::make(c, d));
        }
    }

    void visit(const Div *op) {

        op->a.accept(this);
        Expr min_a = min, max_a = max;
        if (!min_a.defined() || !max_a.defined()) {
            min = Expr(); max = Expr(); return;
        }

        op->b.accept(this);
        Expr min_b = min, max_b = max;
        if (!min_b.defined() || !max_b.defined()) {
            min = Expr(); max = Expr(); return;
        }

        if (min_b.same_as(max_b)) {
            // Constant denominator
            Expr a = min_a / min_b;
            Expr b = max_a / max_b;
            Expr cmp = min_b > make_zero(min_b.type());
            min = select(cmp, a, b);
            max = select(cmp, b, a);
        } else {
            // if we can't statically prove that the divisor can't span zero, then we're unbounded
            Expr min_is_positive = simplify(min_b > make_zero(min_b.type()));
            Expr max_is_negative = simplify(max_b < make_zero(max_b.type()));
            if (!equal(min_b, max_b) &&
                !equal(min_is_positive, const_true()) &&
                !equal(max_is_negative, const_true())) {
                min = Expr();
                max = Expr();
                return;
            }

            // Divisor is either strictly positive or strictly
            // negative, so we can just take the extrema.
            Expr a = min_a / min_b;
            Expr b = min_a / max_b;
            Expr c = max_a / min_b;
            Expr d = max_a / max_b;

            min = Min::make(Min::make(a, b), Min::make(c, d));
            max = Max::make(Max::make(a, b), Max::make(c, d));
        }
    }

    void visit(const Mod *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;

        op->b.accept(this);
        Expr min_b = min, max_b = max;
        if (!min_b.defined() || !max_b.defined()) {
            min = Expr(); max = Expr(); return;
        }

        if (min_a.defined() && min_a.same_as(max_a) && min_b.same_as(max_b)) {
            min = max = Mod::make(min_a, min_b);
        } else {
            // Only consider B (so A can be undefined)
            min = make_zero(op->type);
            max = max_b;
            if (!max.type().is_float()) {
                // Integer modulo returns at most one less than the
                // second arg.
                max = max - make_one(op->type);
            }
        }
    }

    void visit(const Min *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        debug(3) << "Bounds of " << Expr(op) << "\n";

        if (min_a.defined() && min_a.same_as(min_b) &&
            max_a.defined() && max_a.same_as(max_b)) {
            min = max = Min::make(min_a, min_b);
            return;
        }

        if (min_a.defined() && min_b.defined()) {
            min = Min::make(min_a, min_b);
        } else {
            min = Expr();
        }

        if (max_a.defined() && max_b.defined()) {
            max = Min::make(max_a, max_b);
        } else {
            max = max_a.defined() ? max_a : max_b;
        }

        debug(3) << min << ", " << max << "\n";
    }


    void visit(const Max *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        debug(3) << "Bounds of " << Expr(op) << "\n";

        if (min_a.defined() && min_a.same_as(min_b) &&
            max_a.defined() && max_a.same_as(max_b)) {
            min = max = Min::make(min_a, min_b);
            return;
        }

        if (min_a.defined() && min_b.defined()) {
            min = Max::make(min_a, min_b);
        } else {
            min = min_a.defined() ? min_a : min_b;
        }

        if (max_a.defined() && max_b.defined()) {
            max = Max::make(max_a, max_b);
        } else {
            max = Expr();
        }

        debug(3) << min << ", " << max << "\n";
    }

    void visit(const EQ *) {
        //assert(false && "Bounds of boolean");
        min = Expr();
        max = Expr();
    }

    void visit(const NE *) {
        // assert(false && "Bounds of boolean");
        min = Expr();
        max = Expr();
    }

    void visit(const LT *) {
        // assert(false && "Bounds of boolean");
        min = Expr();
        max = Expr();
    }

    void visit(const LE *) {
        // assert(false && "Bounds of boolean");
        min = Expr();
        max = Expr();
    }

    void visit(const GT *) {
        // assert(false && "Bounds of boolean");
        min = Expr();
        max = Expr();
    }

    void visit(const GE *) {
        // assert(false && "Bounds of comparison");
        min = Expr();
        max = Expr();
    }

    void visit(const And *) {
        // assert(false && "Bounds of comparison");
        min = Expr();
        max = Expr();
    }

    void visit(const Or *) {
        // assert(false && "Bounds of comparison");
        min = Expr();
        max = Expr();
    }

    void visit(const Not *) {
        // assert(false && "Bounds of comparison");
        min = Expr();
        max = Expr();
    }

    void visit(const Select *op) {
        op->true_value.accept(this);
        Expr min_a = min, max_a = max;
        if (!min_a.defined() || !max_a.defined()) {
            min = Expr(); max = Expr(); return;
        }

        op->false_value.accept(this);
        Expr min_b = min, max_b = max;
        if (!min_b.defined() || !max_b.defined()) {
            min = Expr(); max = Expr(); return;
        }

        min = Min::make(min_a, min_b);

        if (min_a.same_as(max_a) && min_b.same_as(max_b)) {
            max = min;
        } else {
            max = Max::make(max_a, max_b);
        }
    }

    void visit(const Load *op) {
        op->index.accept(this);
        if (min.defined() && min.same_as(max)) {
            // If the index is const we can return the load of that index
            min = max = Load::make(op->type, op->name, min, op->image, op->param);
        } else {
            // Otherwise use the bounds of the type
            bounds_of_type(op->type);
        }
    }

    void visit(const Ramp *op) {
        assert(false && "Bounds of vector");
    }

    void visit(const Broadcast *) {
        assert(false && "Bounds of vector");
    }

    void visit(const Call *op) {
        // If the args are const we can return the call of those args
        std::vector<Expr> new_args(op->args.size());
        bool const_args = true;
        for (size_t i = 0; i < op->args.size() && const_args; i++) {
            op->args[i].accept(this);
            if (min.defined() && min.same_as(max)) {
                new_args[i] = min;
            } else {
                const_args = false;
            }
        }

        if (const_args) {
            min = max = Call::make(op->type, op->name, new_args, op->call_type,
                                   op->func, op->image, op->param);
        } else {
            // Just use the bounds of the type
            bounds_of_type(op->type);
        }
    }

    void visit(const Let *op) {
        op->value.accept(this);
        scope.push(op->name, Interval(min, max));
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

Interval bounds_of_expr_in_scope(Expr expr, const Scope<Interval> &scope) {
    debug(3) << "computing bounds_of_expr_in_scope " << expr << "\n";
    Bounds b;
    b.scope = scope;
    expr.accept(&b);
    debug(3) << "bounds_of_expr_in_scope " << expr << " = " << b.min << ", " << b.max << "\n";
    return Interval(b.min, b.max);
}

Interval interval_union(const Interval &a, const Interval &b) {
    Expr max, min;
    debug(3) << "Interval union of " << a.min << ", " << a.max << ",  " << b.min << ", " << b.max << "\n";
    if (a.max.defined() && b.max.defined()) max = Max::make(a.max, b.max);
    if (a.min.defined() && b.min.defined()) min = Min::make(a.min, b.min);
    return Interval(min, max);
}

Region region_union(const Region &a, const Region &b) {
    assert(a.size() == b.size() && "Mismatched dimensionality in region union");
    Region result;
    for (size_t i = 0; i < a.size(); i++) {
        Expr min = Min::make(a[i].min, b[i].min);
        Expr max_a = a[i].min + a[i].extent;
        Expr max_b = b[i].min + b[i].extent;
        Expr max_plus_one = Max::make(max_a, max_b);
        Expr extent = max_plus_one - min;
        result.push_back(Range(simplify(min), simplify(extent)));
        //result.push_back(Range(min, extent));
    }
    return result;
}

class RegionTouched : public IRVisitor {
public:
    // The bounds of things in scope
    Scope<Interval> scope;

    // If this is non-empty, we only care about this one function
    string func;

    // Min, Max per dimension of each function found. Used if func is empty
    map<string, vector<Interval> > regions;

    // Min, Max per dimension of func, if it is non-empty
    vector<Interval> region;

    // Take into account call nodes
    bool consider_calls;

    // Take into account provide nodes
    bool consider_provides;

    // Which buffers are we inside the update step of? We ignore
    // recursive calls from a function to itself to avoid recursive
    // bounds expressions. These bounds are handled during lowering
    // instead.
    Scope<int> inside_update;
private:
    using IRVisitor::visit;

    void visit(const LetStmt *op) {
        op->value.accept(this);
        Interval value_bounds = bounds_of_expr_in_scope(op->value, scope);
        scope.push(op->name, value_bounds);
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const Let *op) {
        op->value.accept(this);
        Interval value_bounds = bounds_of_expr_in_scope(op->value, scope);
        scope.push(op->name, value_bounds);
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const For *op) {
        op->min.accept(this);
        op->extent.accept(this);
        Interval min_bounds = bounds_of_expr_in_scope(op->min, scope);
        Interval extent_bounds = bounds_of_expr_in_scope(op->extent, scope);
        Expr min = min_bounds.min;
        Expr max = (min_bounds.max + extent_bounds.max) - 1;
        scope.push(op->name, Interval(min, max));
        debug(3) << "Adding " << op->name << " to scope\n";
        op->body.accept(this);
        debug(3) << "Removing " << op->name << " from scope\n";
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

        // Don't consider calls to intrinsics, because they're
        // polymorphic, so different calls with have bounds of
        // different types, so they can't be unified.
        if (op->call_type == Call::Intrinsic) {
            return;
        }

        if (consider_calls && !inside_update.contains(op->name) &&
            (func.empty() || func == op->name)) {
            debug(3) << "Found call to " << op->name << ": " << Expr(op) << "\n";

            vector<Interval> &r = func.empty() ? regions[op->name] : region;
            for (size_t i = 0; i < op->args.size(); i++) {
                Interval bounds = bounds_of_expr_in_scope(op->args[i], scope);
                debug(3) << "Bounds of call to " << op->name << " in dimension " << i << ": "
                       << bounds.min << ", " << bounds.max << "\n";
                if (r.size() > i) {
                    r[i] = interval_union(r[i], bounds);
                } else {
                    r.push_back(bounds);
                }
            }
        }
    }

    void visit(const Provide *op) {
        IRVisitor::visit(op);
        if (consider_provides && (func.empty() || func == op->name)) {
            vector<Interval> &r = func.empty() ? regions[op->name] : region;
            for (size_t i = 0; i < op->args.size(); i++) {
                Interval bounds = bounds_of_expr_in_scope(op->args[i], scope);
                if (r.size() > i) {
                    r[i] = interval_union(r[i], bounds);
                } else {
                    r.push_back(bounds);
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

// Convert from (min, max) to (min, extent)
Range interval_to_range(const Interval &i) {
    if (!i.min.defined() || !i.max.defined()) {
        return Range();
    } else {
        return Range(simplify(i.min),
                     simplify((i.max + 1) - i.min));
    }
}

Region compute_region_touched(Stmt s, bool consider_calls, bool consider_provides, const string &func) {
    RegionTouched r;
    r.consider_calls = consider_calls;
    r.consider_provides = consider_provides;
    r.func = func;
    s.accept(&r);
    const vector<Interval> &box = r.region;
    Region region;
    for (size_t i = 0; i < box.size(); i++) {
        region.push_back(interval_to_range(box[i]));
    }
    return region;
}

map<string, Region> compute_regions_touched(Stmt s, bool consider_calls, bool consider_provides) {
    RegionTouched r;
    r.consider_calls = consider_calls;
    r.consider_provides = consider_provides;
    r.func = "";
    s.accept(&r);
    map<string, Region> regions;
    for (map<string, vector<Interval> >::iterator iter = r.regions.begin();
         iter != r.regions.end(); ++iter) {
        Region region;
        const vector<Interval> &box = iter->second;
        for (size_t i = 0; i < box.size(); i++) {
            region.push_back(interval_to_range(box[i]));
        }
        regions[iter->first] = region;
    }
    return regions;
}

map<string, Region> regions_provided(Stmt s) {
    return compute_regions_touched(s, false, true);
}

map<string, Region> regions_called(Stmt s) {
    return compute_regions_touched(s, true, false);
}

map<string, Region> regions_touched(Stmt s) {
    return compute_regions_touched(s, true, true);
}

Region region_provided(Stmt s, const string &func) {
    return compute_region_touched(s, false, true, func);
}

Region region_called(Stmt s, const string &func) {
    return compute_region_touched(s, true, false, func);
}

Region region_touched(Stmt s, const string &func) {
    return compute_region_touched(s, true, true, func);
}


void check(const Scope<Interval> &scope, Expr e, Expr correct_min, Expr correct_max) {
    Interval result = bounds_of_expr_in_scope(e, scope);
    if (result.min.defined()) result.min = simplify(result.min);
    if (result.max.defined()) result.max = simplify(result.max);
    bool success = true;
    if (!equal(result.min, correct_min)) {
        std::cout << "Incorrect min: " << result.min << std::endl
                  << "Should have been: " << correct_min << std::endl;
        success = false;
    }
    if (!equal(result.max, correct_max)) {
        std::cout << "Incorrect max: " << result.max << std::endl
                  << "Should have been: " << correct_max << std::endl;
        success = false;
    }
    assert(success && "Bounds test failed");
}

void bounds_test() {
    Scope<Interval> scope;
    Var x("x"), y("y");
    scope.push("x", Interval(Expr(0), Expr(10)));

    check(scope, x, 0, 10);
    check(scope, x+1, 1, 11);
    check(scope, (x+1)*2, 2, 22);
    check(scope, x*x, 0, 100);
    check(scope, 5-x, -5, 5);
    check(scope, x*(5-x), -50, 50); // We don't expect bounds analysis to understand correlated terms
    check(scope, Select::make(x < 4, x, x+100), 0, 110);
    check(scope, x+y, y, y+10);
    check(scope, x*y, select(0 < y, 0, y*10), select(0 < y, y*10, 0));
    check(scope, x/(x+y), Expr(), Expr());
    check(scope, 11/(x+1), 1, 11);
    check(scope, Load::make(Int(8), "buf", x, Buffer(), Parameter()), cast(Int(8), -128), cast(Int(8), 127));
    check(scope, y + (Let::make("y", x+3, y - x + 10)), y + 3, y + 23); // Once again, we don't know that y is correlated with x
    check(scope, clamp(1/(x-2), x-10, x+10), -10, 20);

    vector<Expr> input_site_1 = vec(2*x);
    vector<Expr> input_site_2 = vec(2*x+1);
    vector<Expr> output_site = vec(x+1);

    Stmt loop = For::make("x", 3, 10, For::Serial,
                        Provide::make("output",
                                    Add::make(
                                        Call::make(Int(32), "input", input_site_1, Call::Extern),
                                        Call::make(Int(32), "input", input_site_2, Call::Extern)),
                                    output_site));

    map<string, Region> r;
    r = regions_called(loop);
    assert(r.find("output") == r.end());
    assert(r.find("input") != r.end());
    assert(equal(r["input"][0].min, 6));
    assert(equal(r["input"][0].extent, 20));
    r = regions_provided(loop);
    assert(r.find("output") != r.end());
    assert(equal(r["output"][0].min, 4));
    assert(equal(r["output"][0].extent, 10));

    Region r2 = vec(Range(Expr(5), Expr(15)));
    r2 = region_union(r["output"], r2);
    assert(equal(r2[0].min, 4));
    assert(equal(r2[0].extent, 16));

    std::cout << "Bounds test passed" << std::endl;
}

}
}
