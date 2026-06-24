#include "Inductive.h"

#include "Bounds.h"
#include "ConciseCasts.h"
#include "Error.h"
#include "Function.h"
#include "IR.h"
#include "IREquality.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

class BaseCaseSolver : public IRVisitor {
    using IRVisitor::visit;
    const vector<string> &vars;
    const string &func;

    const vector<Interval> &start_box;

    vector<Interval> condition_intervals;

    Scope<Interval> bounds;

    int nested_select = 0;

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::if_then_else)) {
            // Theoretically there is no need to check op->args[0].
            // Select nodes are only converted to if_then_else when the condition is pure,
            // which means the condition cannot have any recursive calls.
            // std::cout<<"cond is" << op->args[0];
            op->args[0].accept(this);

            bool left_recurse = false, right_recurse = false;
            visit_with(op->args[1], [&](auto *self, const Call *inner_op) {
                if (inner_op->name == func) {
                    left_recurse = true;
                }
                self->visit_base(inner_op);
            });
            visit_with(op->args[2], [&](auto *self, const Call *inner_op) {
                if (inner_op->name == func) {
                    right_recurse = true;
                }
                self->visit_base(inner_op);
            });
            // Again, this check is theoretically unnecessary
            user_assert(!(left_recurse && right_recurse)) << "Select node in inductive function " << func << " does not have a base case";

            nested_select += 1;
            vector<Interval> old_intervals = condition_intervals;
            if (left_recurse) {
                for (size_t i = 0; i < vars.size(); i++) {
                    Interval inter = Interval::make_intersection(old_intervals[i], solve_for_outer_interval(simplify(op->args[0]), vars[i]));
                    condition_intervals[i] = Interval(inter.min, Interval::pos_inf());
                    bounds.push(vars[i], condition_intervals[i]);
                }
                op->args[1].accept(this);
                for (const auto &var : vars) {
                    bounds.pop(var);
                }
            }
            if (right_recurse) {
                for (size_t i = 0; i < vars.size(); i++) {
                    Interval inter = Interval::make_intersection(old_intervals[i], solve_for_outer_interval(simplify(!op->args[0]), vars[i]));
                    condition_intervals[i] = Interval(inter.min, Interval::pos_inf());
                    bounds.push(vars[i], condition_intervals[i]);
                }
                op->args[2].accept(this);
                for (const auto &var : vars) {
                    bounds.pop(var);
                }
            }
            condition_intervals = old_intervals;
            nested_select -= 1;
        } else if (op->name == func) {
            user_assert(nested_select > 0) << "Function " << func << " contains an inductive function reference outside of a select operation value.\n";
            user_assert(nested_select == 1) << "Function " << func << " contains an inductive function reference inside a nested select operation.\n";
            bool found_inductive = false;
            for (size_t position = 0; position < vars.size(); position++) {
                const Expr inductive_expr = op->args[position];
                const Expr new_v = Variable::make(inductive_expr.type(), vars[position]);
                const Expr gets_lower = simplify(new_v - inductive_expr > 0, bounds);
                const Interval i_lower = solve_for_inner_interval(gets_lower, vars[position]);

                Interval new_interval;
                if (equal(new_v, inductive_expr)) {
                    new_interval = start_box[position];
                } else if (i_lower.is_everything()) {
                    found_inductive = true;
                    new_interval = Interval(Interval::neg_inf(), start_box[position].max);
                } else {
                    std::ostringstream err;
                    err << "Inductive variable " << vars[position] << " in inductive function " << func << " is not provably monotonically decreasing outside of the base case.";
                    user_error << err.str() << "\n";
                }
                new_interval = Interval::make_intersection(new_interval, condition_intervals[position]);
                Scope<Interval> i_scope;
                i_scope.push(vars[position], new_interval);
                result_intervals[position] = Interval::make_union(result_intervals[position], Interval::make_union(new_interval, bounds_of_expr_in_scope(inductive_expr, i_scope)));
            }
            user_assert(found_inductive) << "Unable to prove in inductive function " << func << " that the inductive step is monotonically decreasing.\n";

            IRVisitor::visit(op);

        } else {
            IRVisitor::visit(op);
        }
    }

public:
    vector<Interval> result_intervals;

    BaseCaseSolver(const vector<string> &v, const string &func, const vector<Interval> &con)
        : vars(v), func(func), start_box(con) {
        condition_intervals = vector<Interval>(start_box.size());
        result_intervals = vector<Interval>(start_box.size(), Interval::nothing());
    }
};

}  // anonymous namespace

Box expand_to_include_base_case(const vector<string> &vars, const Expr &RHS, const string &func, const Box &box_required) {
    Expr substed = substitute_in_all_lets(RHS);
    Box box2 = box_required;
    BaseCaseSolver b(vars, func, box_required.bounds);
    substed.accept(&b);
    for (size_t i = 0; i < vars.size(); i++) {
        user_assert(b.result_intervals[i].is_bounded() || b.result_intervals[i].is_empty()) << "Unable to prove that the inductive function " << func << " uses a bounded interval";
        if (!b.result_intervals[i].is_empty()) {
            Interval new_interval(min(b.result_intervals[i].min, box_required[i].min), box_required[i].max);
            box2[i] = new_interval;
        }
    }

    return box2;
}

}  // namespace Internal
}  // namespace Halide
