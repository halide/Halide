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
            nested_select += 1;
            vector<Interval> old_intervals = condition_intervals;
            for (size_t i = 0; i < vars.size(); i++) {
                condition_intervals[i] = Interval::make_intersection(old_intervals[i], solve_for_outer_interval(simplify(op->args[0]), vars[i]));
                bounds.push(vars[i], condition_intervals[i]);
            }

            op->args[1].accept(this);
            for (size_t i = 0; i < vars.size(); i++) {
                condition_intervals[i] = Interval::make_intersection(old_intervals[i], solve_for_outer_interval(simplify(!op->args[0]), vars[i]));
                bounds.pop(vars[i]);
                bounds.push(vars[i], condition_intervals[i]);
            }
            op->args[2].accept(this);
            condition_intervals = old_intervals;
            for (const auto &var : vars) {
                bounds.pop(var);
            }
            nested_select -= 1;
        } else if (op->name == func) {
            user_assert(nested_select > 0) << "Function " << func << " contains an inductive function reference outside of a select operation.\n";
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
                    new_interval = Interval::everything();
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

// anonymous namespace

Box expand_to_include_base_case(const vector<string> &vars, const Expr &RHS, const string &func, const Box &box_required) {
    Expr substed = substitute_in_all_lets(RHS);
    Box box2 = box_required;
    BaseCaseSolver b(vars, func, box_required.bounds);
    substed.accept(&b);
    for (size_t i = 0; i < vars.size(); i++) {
        user_assert(b.result_intervals[i].is_bounded()) << "Unable to prove that the inductive function " << func << " uses a bounded interval";
        Interval new_interval(min(b.result_intervals[i].min, box_required[i].min), box_required[i].max);
        box2[i] = new_interval;
    }

    return box2;
}

Box expand_to_include_base_case(const Function &fn, const Box &box_required, const int &pos) {
    return expand_to_include_base_case(fn.args(), fn.values()[pos], fn.name(), box_required);
}

Box expand_to_include_base_case(const Function &fn, const Box &box_required) {
    Box b = expand_to_include_base_case(fn.args(), fn.values()[0], fn.name(), box_required);
    for (size_t pos = 1; pos < fn.values().size(); pos++) {
        Box b2 = expand_to_include_base_case(fn.args(), fn.values()[pos], fn.name(), box_required);
        merge_boxes(b, b2);
    }
    return b;
}

}  // namespace Internal
}  // namespace Halide
