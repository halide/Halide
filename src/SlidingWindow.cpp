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

            // We're interested in the case where exactly one of the
            // dimensions of the buffer has a min/extent that depends
            // on the loop_var.
            string dim = "";
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
                        min_produced = Expr();
                        extent_produced = Expr();
                        break;
                    } else {
                        dim = func.args()[i];
                        min_produced = substitute(min_req_name, m_r, m);
                        extent_produced = substitute(extent_req_name, e_r, e);
                    }
                }
            }

            Expr loop_var_expr = Variable::make(Int(32), loop_var);
            bool increasing = true;

            if (min_produced.defined()) {
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
                    min_produced = Expr();
                }
            }

            if (min_produced.defined()) {
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

                if (increasing) {

                    Expr max_plus_one = min_produced + extent_produced;
                    Expr prev_max_plus_one = substitute(loop_var, loop_var_expr - 1, max_plus_one);

                    new_extent = max_plus_one - prev_max_plus_one;

                    // We still need to produce an amount that is a
                    // multiple of the min_extent_factor and is at
                    // least the min_extent.
                    new_extent = Max::make(new_extent, min_extent);
                    new_extent += min_extent_factor - 1;
                    new_extent /= min_extent_factor;
                    new_extent *= min_extent_factor;
                    new_extent = simplify(new_extent);

                    new_min = max_plus_one - new_extent;

                    new_extent = select(loop_var_expr == loop_min, extent_produced, new_extent);
                    new_min = select(loop_var_expr == loop_min, min_produced, new_min);

                } else {
                    Expr prev_min = substitute(loop_var, loop_var_expr - 1, min_produced);

                    new_extent = prev_min - min_produced;
                    new_extent = Max::make(new_extent, min_extent);
                    new_extent += min_extent_factor - 1;
                    new_extent /= min_extent_factor;
                    new_extent *= min_extent_factor;
                    new_extent = simplify(new_extent);

                    new_extent = select(loop_var_expr == loop_min, extent_produced, new_extent);
                    new_min = min_produced;
                }

                new_min = simplify(new_min);
                new_extent = simplify(new_extent);

                debug(3) << "Sliding " << func.name() << ", " << dim << "\n"
                         << "Pushing min up from " << min_produced << " to " << new_min << "\n"
                         << "Shrinking extent from " << extent_produced << " to " << new_extent << "\n";
                string min_name = func.name() + "." + dim + ".min_produced";
                string extent_name = func.name() + "." + dim + ".extent_produced";

                stmt = op;
                stmt = LetStmt::make(min_name, new_min, stmt);
                stmt = LetStmt::make(extent_name, new_extent, stmt);

                debug(3) << "new extent: " << new_extent << "\n";

            } else {
                debug(3) << "Could not perform sliding window optimization of "
                         << func.name() << " over " << loop_var << "\n";
                stmt = op;
            }


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

        // For now we skip reductions, because the update step of a
        // reduction may read from the same buffer at points outside
        // of the region required, which in turn means the
        // initialization takes place over a larger domain than the
        // region required, which means that the initialization may
        // clobber old values that we're relying on to still be
        // correct.
        //
        // In the future we may allow a more nuanced test here.

        Stmt new_body = op->body;
        if (!(iter->second.has_reduction_definition())) {
            debug(3) << "Doing sliding window analysis on realization of " << op->name << "\n";
            new_body = SlidingWindowOnFunction(iter->second).mutate(new_body);
        }
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
