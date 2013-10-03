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
            Expr min, extent;

            Region r = region_called(op, func.name());

            debug(3) << "Considering sliding " << func.name()
                     << " along loop variable " << loop_var << "\n"
                     << "Region provided:\n";
            for (size_t i = 0; i < r.size(); i++) {
                debug(3) << i << ") " << r[i].min << ", " << r[i].extent << "\n";
                if (expr_depends_on_var(r[i].min, loop_var) ||
                    expr_depends_on_var(r[i].extent, loop_var)) {
                    if (!dim.empty()) {
                        dim = "";
                        min = Expr();
                        extent = Expr();
                        break;
                    } else {
                        dim = func.args()[i];
                        min = r[i].min;
                        extent = r[i].extent;
                    }
                }
            }

            Expr loop_var_expr = Variable::make(Int(32), loop_var);
            bool increasing = true;

            if (min.defined()) {
                MonotonicResult m = is_monotonic(min, loop_var);

                if (m == MonotonicIncreasing || m == Constant) {

                } else if (m == MonotonicDecreasing) {
                    increasing = false;
                } else {
                    debug(3) << "Not sliding " << func.name()
                             << " over dimension " << dim
                             << " along loop variable " << loop_var
                             << " because I couldn't prove it moved monotonically along that dimension\n"
                             << "Min is " << min << "\n";
                    min = Expr();
                }
            }

            if (min.defined()) {
                // Ok, we've isolated a function, a dimension to slide along, and loop variable to slide over
                debug(3) << "Sliding " << func.name() << " over dimension " << dim << " along loop variable " << loop_var << "\n";
                Expr steady_state = loop_var_expr > loop_min;

                Expr new_min, new_extent;

                if (increasing) {
                    Expr prev_max_plus_one = substitute(loop_var, loop_var_expr - 1, min + extent);

                    // Bump up the min to skip stuff we've already computed.
                    new_min = simplify(Max::make(min, prev_max_plus_one));

                    // The new extent is the old extent shrunk by how
                    // much we trimmed off the min. The extent does
                    // not get larger because new_min > min.
                    //new_extent = simplify(extent + (min - new_min));
                    new_extent = simplify(extent + Min::make(min - prev_max_plus_one, 0));
                } else {
                    // Truncate the extent to be less than the previous min
                    new_min = simplify(min);
                    Expr prev_min = substitute(loop_var, loop_var_expr - 1, min);
                    new_extent = simplify(Min::make(extent + min, prev_min) - new_min);
                }

                debug(3) << "Pushing min up from " << min << " to " << new_min << "\n";
                debug(3) << "Shrinking extent from " << extent << " to " << new_extent << "\n";
                string min_name = func.name() + "." + dim + ".min_produced";
                string extent_name = func.name() + "." + dim + ".extent_produced";

                // Check if we've gone off the end. Only relevant for increasing.
                if (increasing) {
                    // TODO: It would be better to truncate back to
                    // the max min, but this produces expressions too
                    // difficult for storage folding to handle.
                    Expr min_extent = func.min_extent_produced(dim);
                    if (!is_one(min_extent)) {
                        //Expr max_min = Variable::make(Int(32), func.name() + "." + dim + ".max_min");
                        Expr extent_produced = Variable::make(Int(32), func.name() + "." + dim + ".extent_realized");
                        Expr min_produced = Variable::make(Int(32), func.name() + "." + dim + ".min_realized");
                        Expr max_min = min_produced + extent_produced - min_extent;
                        Expr before_end = new_min < max_min;
                        steady_state = steady_state && before_end;
                    }
                }

                stmt = op;
                if (increasing) {
                    new_min = Select::make(steady_state, new_min, min);
                    stmt = LetStmt::make(min_name, new_min, stmt);
                }
                new_extent = Select::make(steady_state, new_extent, extent);
                //new_extent = extent;
                stmt = LetStmt::make(extent_name, new_extent, stmt);

                if (increasing) {

                }

            } else {
                debug(3) << "Could not perform sliding window optimization of " << func.name() << " over " << loop_var << "\n";
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

        Stmt new_body = op->body;
        if (iter != env.end()) {
            debug(3) << "Doing sliding window analysis on realization of " << op->name << "\n";
            new_body = SlidingWindowOnFunction(iter->second).mutate(new_body);
        } else {
            assert(false && "Compiler bug: Sliding window found a realization for a function not in the environment\n");
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
