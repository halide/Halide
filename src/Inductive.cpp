#include "Inductive.h"

#include "Bounds.h"
#include "Error.h"
#include "ExprUsesVar.h"
#include "Function.h"
#include "IR.h"
#include "IREquality.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Simplify.h"

#include <set>

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
    const vector<bool> &is_inductive_var;
    const bool is_update;

    vector<Interval> condition_intervals;

    Scope<Interval> bounds;

    // The inductive vars, and the enclosing lets that depend on them.
    Scope<> inductive_vars;

    // The enclosing lets whose values refer to the function. These are
    // checked where they are used, once per select arm.
    Scope<Expr> recursive_lets;
    int arm = 0, arms = 0;
    std::set<std::pair<string, int>> checked_lets;

    // The enclosing lets, outermost first.
    vector<std::pair<string, Expr>> lets;

    int nested_select = 0;

    bool recurses(const Expr &e) const {
        bool result = false;
        visit_with(
            e,
            [&](auto *self, const Call *op) {
                result |= op->name == func;
                self->visit_base(op);
            },
            [&](auto *, const Variable *op) {
                result |= recursive_lets.contains(op->name);
            });
        return result;
    }

    Expr in_context(const Expr &e) const {
        return rewrap_used_lets(e, lets);
    }

    void visit_arm(const Expr &e) {
        ScopedValue<int> old_arm(arm, ++arms);
        e.accept(this);
    }

    void visit(const Variable *op) override {
        if (recursive_lets.contains(op->name) && checked_lets.emplace(op->name, arm).second) {
            recursive_lets.get(op->name).accept(this);
        }
    }

    void visit(const Let *op) override {
        bool recursive = recurses(op->value);
        if (!recursive) {
            op->value.accept(this);
        }
        ScopedBinding<> bind_inductive(expr_uses_vars(op->value, inductive_vars), inductive_vars, op->name);
        ScopedBinding<Expr> bind_recursive(recursive, recursive_lets, op->name, op->value);
        lets.emplace_back(op->name, op->value);
        op->body.accept(this);
        lets.pop_back();
    }

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::if_then_else)) {

            // A select whose condition doesn't depend on any inductive var
            // can be traversed normally. So can one whose condition reads
            // the function itself: that is a data-dependent step of the
            // recurrence, not a base-case guard, and both of its branches
            // may recurse (each self-reference in them is still checked).
            bool cond_is_inductive = expr_uses_vars(op->args[0], inductive_vars);
            if (!cond_is_inductive || recurses(op->args[0])) {
                IRVisitor::visit(op);
                return;
            }

            // Theoretically there is no need to check op->args[0].
            // Select nodes are only converted to if_then_else when the condition is pure,
            // which means the condition cannot have any recursive calls.
            op->args[0].accept(this);

            bool left_recurse = recurses(op->args[1]);
            bool right_recurse = recurses(op->args[2]);
            Expr cond = simplify(in_context(op->args[0]));
            // Again, this check is theoretically unnecessary
            user_assert(!(left_recurse && right_recurse)) << "Select node in inductive function " << func << " does not have a base case";

            nested_select += 1;
            vector<Interval> old_intervals = condition_intervals;
            if (left_recurse) {
                for (size_t i = 0; i < vars.size(); i++) {
                    Interval inter = Interval::make_intersection(old_intervals[i], solve_for_outer_interval(cond, vars[i]));
                    condition_intervals[i] = Interval(inter.min, Interval::pos_inf());
                    bounds.push(vars[i], condition_intervals[i]);
                }
                visit_arm(op->args[1]);
                for (const auto &var : vars) {
                    bounds.pop(var);
                }
            }
            if (right_recurse) {
                for (size_t i = 0; i < vars.size(); i++) {
                    Interval inter = Interval::make_intersection(old_intervals[i], solve_for_outer_interval(simplify(!cond), vars[i]));
                    condition_intervals[i] = Interval(inter.min, Interval::pos_inf());
                    bounds.push(vars[i], condition_intervals[i]);
                }
                visit_arm(op->args[2]);
                for (const auto &var : vars) {
                    bounds.pop(var);
                }
            }
            condition_intervals = old_intervals;
            nested_select -= 1;
        } else if (op->name == func) {
            // An update definition's self-reference whose inductive args match the
            // pure vars exactly is acceptable.
            if (is_update) {
                bool exact_pure_self_reference = true;
                for (size_t position = 0; position < vars.size() && exact_pure_self_reference; position++) {
                    if (is_inductive_var[position] &&
                        (position >= op->args.size() || !equal(simplify(in_context(op->args[position])), Variable::make(op->args[position].type(), vars[position])))) {
                        exact_pure_self_reference = false;
                    }
                }
                if (exact_pure_self_reference) {
                    IRVisitor::visit(op);
                    return;
                }
            }

            user_assert(nested_select > 0) << "Function " << func << " contains an inductive function reference outside of a select operation value.\n";
            user_assert(nested_select == 1) << "Function " << func << " contains an inductive function reference inside a nested select operation.\n";
            bool found_inductive = false;
            for (size_t position = 0; position < vars.size(); position++) {
                const Expr inductive_expr = simplify(in_context(op->args[position]));
                if (!is_inductive_var[position]) {
                    continue;
                }
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

    BaseCaseSolver(const vector<string> &v, const vector<bool> &is_inductive_var, const string &func, const vector<Interval> &con, bool is_update)
        : vars(v), func(func), start_box(con), is_inductive_var(is_inductive_var), is_update(is_update) {
        condition_intervals = vector<Interval>(start_box.size());
        result_intervals = vector<Interval>(start_box.size(), Interval::nothing());
        for (size_t i = 0; i < vars.size(); i++) {
            if (is_inductive_var[i]) {
                inductive_vars.push(vars[i]);
            }
        }
    }
};

}  // anonymous namespace

Box expand_to_include_base_case(const vector<string> &vars, const vector<bool> &is_inductive_var,
                                const Expr &RHS, const string &func, const Box &box_required,
                                bool is_update) {
    Box box2 = box_required;
    BaseCaseSolver b(vars, is_inductive_var, func, box_required.bounds, is_update);
    RHS.accept(&b);

    Scope<Interval> non_inductive_scope;
    for (size_t i = 0; i < vars.size(); i++) {
        if (!is_inductive_var[i]) {
            non_inductive_scope.push(vars[i], box_required[i]);
        }
    }

    for (size_t i = 0; i < vars.size(); i++) {
        user_assert(b.result_intervals[i].is_bounded() || b.result_intervals[i].is_empty()) << "Unable to prove that the inductive function " << func << " uses a bounded interval";
        if (!b.result_intervals[i].is_empty()) {
            Interval new_interval(min(b.result_intervals[i].min, box_required[i].min), box_required[i].max);

            new_interval.min = bounds_of_expr_in_scope(new_interval.min, non_inductive_scope).min;
            box2[i] = new_interval;
        }
    }

    return box2;
}

}  // namespace Internal
}  // namespace Halide
