#include "SlidingWindow.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Debug.h"
#include "Substitute.h"
#include "IRPrinter.h"
#include "Simplify.h"
#include "Derivative.h"
#include "Bounds.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;

// Does an expression depend on a particular variable?
class ExprDependsOnVar : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) {
        if (op->name == var) result = true;
    }

    void visit(const Let *op) {
        op->value.accept(this);
        // The name might be hidden within the body of the let, in
        // which case there's no point descending.
        if (op->name != var) {
            op->body.accept(this);
        }
    }
public:

    bool result;
    string var;

    ExprDependsOnVar(string v) : result(false), var(v) {
    }
};

bool expr_depends_on_var(Expr e, string v) {
    ExprDependsOnVar depends(v);
    e.accept(&depends);
    return depends.result;
}

// Perform sliding window optimization for a function over a
// particular serial for loop
class SlidingWindowOnFunctionAndLoop : public IRMutator {
    Function func;
    string loop_var;
    Expr loop_min;
    Scope<Expr> scope;

    using IRMutator::visit;

    void visit(const Pipeline *op) {
        if (op->name != func.name()) {
            IRMutator::visit(op);
        } else {

            stmt = op;

            // We're interested in the case where exactly one of the
            // dimensions of the buffer has a min/extent that depends
            // on the loop_var.
            string dim = "";
            int dim_idx = -1;
            Expr min_produced, extent_produced;

            debug(3) << "Considering sliding " << func.name()
                     << " along loop variable " << loop_var << "\n"
                     << "Region provided:\n";

            for (int i = 0; i < func.dimensions(); i++) {
                // Look up the region produced over this dimension
                Expr m = scope.get(func.name() + "." + func.args()[i] + ".min_produced");
                Expr e = scope.get(func.name() + "." + func.args()[i] + ".extent_produced");

                string min_req_name = func.name() + "." + func.args()[i] + ".min_required";
                string extent_req_name = func.name() + "." + func.args()[i] + ".extent_required";
                Expr m_r = scope.get(min_req_name);
                Expr e_r = scope.get(extent_req_name);

                debug(3) << func.args()[i] << ":" << m << ", " << e  << "\n";
                if (expr_depends_on_var(m_r, loop_var) ||
                    expr_depends_on_var(e_r, loop_var)) {
                    if (!dim.empty()) {
                        dim = "";
                        dim_idx = -1;
                        min_produced = Expr();
                        extent_produced = Expr();
                        break;
                    } else {
                        dim = func.args()[i];
                        dim_idx = i;
                        min_produced = substitute(min_req_name, m_r, m);
                        extent_produced = substitute(extent_req_name, e_r, e);
                    }
                }
            }

            if (!min_produced.defined()) {
                debug(3) << "Could not perform sliding window optimization of "
                         << func.name() << " over " << loop_var << " because either zero "
                         << "or many dimensions of the function dependended on the loop var\n";
                return;
            }

            bool increasing = true;

            MonotonicResult m = is_monotonic(min_produced, loop_var);

            if (m == MonotonicIncreasing || m == Constant) {
                increasing = true;
            } else if (m == MonotonicDecreasing) {
                increasing = false;
            } else {
                debug(3) << "Not sliding " << func.name()
                         << " over dimension " << dim
                         << " along loop variable " << loop_var
                         << " because I couldn't prove it moved monotonically along that dimension\n"
                         << "Min is " << min << "\n";
                return;
            }


            if (op->update.defined()) {
                // A reduction may not reach outwards in the dimension
                // that we're trying to slide over. E.g. we can't slide
                // the following over x:
                //
                // f(x, y) = 2
                // f(x, 0) = f(x-1, 1) + f(x+1, 1);

                // The update step of such a reads from the same buffer at
                // points outside of the region required, which in turn
                // means the initialization takes place over a larger
                // domain than the region required, which means that the
                // initialization may clobber old values that we're
                // relying on to still be correct when we slide.
                Region r = region_called(op->update, func.name());
                if (r.size()) {
                    assert(dim_idx < (int)r.size());
                    Expr min_initialized = r[dim_idx].min;
                    Expr extent_initialized = r[dim_idx].extent;

                    assert(min_initialized.defined() && extent_initialized.defined() &&
                           "Function update reads itself in an unbounded manner\n");

                    string min_name = func.name() + "." + dim + ".min_produced";
                    string extent_name = func.name() + "." + dim + ".extent_produced";
                    Expr min_produced_var = Variable::make(Int(32), min_name);
                    Expr extent_produced_var = Variable::make(Int(32), extent_name);

                    Expr test;
                    if (increasing) {
                        test = (min_initialized >= min_produced_var);
                    } else {
                        Expr max_initialized = min_initialized + extent_initialized;
                        Expr max_produced = min_produced_var + extent_produced_var;
                        test = (max_initialized <= max_produced);
                    }
                    Expr simplified_test = simplify(test);

                    if (!is_one(simplified_test)) {
                        debug(3) << "Not sliding " << func.name() << " along " << dim
                                 << " because we failed to prove that the reduction step doesn't trigger an"
                                 << " initialization over a larger domain along that axis. The expression we"
                                 << " attempted to prove was: " << test << "\n"
                                 << "Which simplified to " << simplified_test << "\n";
                        return;
                    }
                }

            }

            // Ok, we've isolated a function, a dimension to slide along, and loop variable to slide over
            debug(3) << "Sliding " << func.name()
                     << " over dimension " << dim
                     << " along loop variable " << loop_var << "\n";

            Expr new_min, new_extent;

            Expr min_extent = func.min_extent_produced(dim);
            Expr min_extent_factor = func.min_extent_updated(dim);

            // We've sworn to produce from min_produced to
            // extent_produced, but we can perhaps skip values
            // already computed

            Expr loop_var_expr = Variable::make(Int(32), loop_var);
            Expr max_plus_one = min_produced + extent_produced;
            // Compute the new extent.
            if (increasing) {
                Expr prev_max_plus_one = substitute(loop_var, loop_var_expr - 1, max_plus_one);
                new_extent = max_plus_one - prev_max_plus_one;
            } else {
                Expr prev_min = substitute(loop_var, loop_var_expr - 1, min_produced);
                new_extent = prev_min - min_produced;
            }

            // We still need to produce an amount that is a
            // multiple of the min_extent_factor and is at
            // least the min_extent.
            new_extent = Max::make(new_extent, min_extent);
            new_extent += min_extent_factor - 1;
            new_extent /= min_extent_factor;
            new_extent *= min_extent_factor;

            // Compute the new min.
            if (increasing) {
                new_min = max_plus_one - new_extent;
                new_min = select(loop_var_expr == loop_min, min_produced, new_min);
            } else {
                new_min = min_produced;
            }

            // Use the old min and extent for the first iteration.
            new_extent = select(loop_var_expr == loop_min, extent_produced, new_extent);

            // Simplify.
            new_extent = simplify(new_extent);
            new_min = simplify(new_min);

            debug(3) << "Sliding " << func.name() << ", " << dim << "\n"
                     << "Pushing min up from " << min_produced << " to " << new_min << "\n"
                     << "Shrinking extent from " << extent_produced << " to " << new_extent << "\n";
            string min_name = func.name() + "." + dim + ".min_produced";
            string extent_name = func.name() + "." + dim + ".extent_produced";

            stmt = LetStmt::make(min_name, new_min, stmt);
            stmt = LetStmt::make(extent_name, new_extent, stmt);
        }
    }

    void visit(const LetStmt *op) {
        scope.push(op->name, op->value);
        Stmt new_body = mutate(op->body);
        if (new_body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, op->value, new_body);
        }
        scope.pop(op->name);
    }

public:
    SlidingWindowOnFunctionAndLoop(Function f, string v, Expr v_min) : func(f), loop_var(v), loop_min(v_min) {}
};

// Perform sliding window optimization for a particular function
class SlidingWindowOnFunction : public IRMutator {
    Function func;

    using IRMutator::visit;

    void visit(const For *op) {
        Stmt new_body = mutate(op->body);

        if (op->for_type == For::Serial || op->for_type == For::Unrolled) {
            new_body = SlidingWindowOnFunctionAndLoop(func, op->name, op->min).mutate(new_body);
        }

        if (new_body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = For::make(op->name, op->min, op->extent, op->for_type, new_body);
        }
    }

public:
    SlidingWindowOnFunction(Function f) : func(f) {}
};

// Perform sliding window optimization for all functions
class SlidingWindow : public IRMutator {
    const map<string, Function> &env;

    using IRMutator::visit;

    void visit(const Realize *op) {
        // Find the args for this function
        map<string, Function>::const_iterator iter = env.find(op->name);

        assert(iter != env.end() && "Compiler bug: Sliding window found a realization for a function not in the environment\n");

        Stmt new_body = op->body;

        debug(3) << "Doing sliding window analysis on realization of " << op->name << "\n";
        new_body = SlidingWindowOnFunction(iter->second).mutate(new_body);

        new_body = mutate(new_body);

        if (new_body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = Realize::make(op->name, op->types, op->bounds, new_body);
        }
    }
public:
    SlidingWindow(const map<string, Function> &e) : env(e) {}

};

Stmt sliding_window(Stmt s, const map<string, Function> &env) {
    return SlidingWindow(env).mutate(s);
}

}
}
