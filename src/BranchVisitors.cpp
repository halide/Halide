#include "BranchVisitors.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LinearSolve.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

// This visitor checks if a Stmt or Expr branches linearly in a set of free variables,
// by which we mean the branching condition depends linearly on the variables.
class BranchesLinearlyInVars : public IRVisitor {
public:
    bool result;

    BranchesLinearlyInVars(const Scope<int>& fv, const Scope<int> *bv, bool minmax) :
            result(false), free_vars(fv), branch_on_minmax(minmax)
    {
        bound_vars.set_containing_scope(bv);
    }

private:
    const Scope<int> &free_vars;
    bool branch_on_minmax;
    Scope<int> bound_vars;

    using IRVisitor::visit;

    void visit(const IfThenElse *op) {
        if (expr_is_linear_in_vars(op->condition, free_vars, bound_vars)) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Select *op) {
        if (expr_is_linear_in_vars(op->condition, free_vars, bound_vars) &&
            op->condition.type().is_scalar()) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Min *op) {
        if (branch_on_minmax &&
            (expr_is_linear_in_vars(op->a, free_vars, bound_vars) ||
             expr_is_linear_in_vars(op->b, free_vars, bound_vars))) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Max *op) {
        if (branch_on_minmax &&
            (expr_is_linear_in_vars(op->a, free_vars, bound_vars) ||
             expr_is_linear_in_vars(op->b, free_vars, bound_vars))) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    template<class LetOp>
    void visit_let(const LetOp *op) {
        bound_vars.push(op->name, expr_linearity(op->value, free_vars, bound_vars));
        op->body.accept(this);
        bound_vars.pop(op->name);
    }

    void visit(const LetStmt *op) {visit_let(op);}
    void visit(const Let *op) {visit_let(op);}
};

bool branches_linearly_in_var(Stmt stmt, const std::string &var, bool branch_on_minmax) {
    Scope<int> free_vars;
    free_vars.push(var, 0);

    BranchesLinearlyInVars has_branches(free_vars, NULL, branch_on_minmax);
    stmt.accept(&has_branches);
    return has_branches.result;
}

bool branches_linearly_in_var(Expr expr, const std::string &var, bool branch_on_minmax) {
    Scope<int> free_vars;
    free_vars.push(var, 0);

    BranchesLinearlyInVars has_branches(free_vars, NULL, branch_on_minmax);
    expr.accept(&has_branches);
    return has_branches.result;
}


bool branches_linearly_in_var(Stmt stmt, const std::string &var, const Scope<int> &bound_vars,
                              bool branch_on_minmax) {
    Scope<int> free_vars;
    free_vars.push(var, 0);

    BranchesLinearlyInVars has_branches(free_vars, &bound_vars, branch_on_minmax);
    stmt.accept(&has_branches);
    return has_branches.result;
}

bool branches_linearly_in_var(Expr expr, const std::string &var, const Scope<int> &bound_vars,
                              bool branch_on_minmax) {
    Scope<int> free_vars;
    free_vars.push(var, 0);

    BranchesLinearlyInVars has_branches(free_vars, &bound_vars, branch_on_minmax);
    expr.accept(&has_branches);
    return has_branches.result;
}


bool branches_linearly_in_vars(Stmt stmt, const Scope<int> &free_vars, bool branch_on_minmax) {
    BranchesLinearlyInVars has_branches(free_vars, NULL, branch_on_minmax);
    stmt.accept(&has_branches);
    return has_branches.result;
}

bool branches_linearly_in_vars(Expr expr, const Scope<int> &free_vars, bool branch_on_minmax) {
    BranchesLinearlyInVars has_branches(free_vars, NULL, branch_on_minmax);
    expr.accept(&has_branches);
    return has_branches.result;
}

bool branches_linearly_in_vars(Stmt stmt, const Scope<int> &free_vars, const Scope<int> &bound_vars,
                               bool branch_on_minmax) {
    BranchesLinearlyInVars has_branches(free_vars, &bound_vars, branch_on_minmax);
    stmt.accept(&has_branches);
    return has_branches.result;
}

bool branches_linearly_in_vars(Expr expr, const Scope<int> &free_vars, const Scope<int> &bound_vars,
                               bool branch_on_minmax) {
    BranchesLinearlyInVars has_branches(free_vars, &bound_vars, branch_on_minmax);
    expr.accept(&has_branches);
    return has_branches.result;
}


// A mutator that "normalizes" IfThenElse and Select nodes. By normalizing these nodes
// we mean converting the conditions to simple inequality constraints whenever possible.
class NormalizeBranches : public IRMutator {
public:
    NormalizeBranches(const Scope<Expr> *s, const int limit = 10) :
            branch_count(0), branching_limit(limit),
            in_if_cond(false), in_select_cond(false) {
        scope.set_containing_scope(s);
    }

private:
    using IRMutator::visit;

    Scope<Expr> scope;

    int branch_count;
    const int branching_limit;

    bool in_if_cond;
    std::stack<Stmt> then_case;
    std::stack<Stmt> else_case;

    bool in_select_cond;
    std::stack<Expr> true_value;
    std::stack<Expr> false_value;

    void visit(const IfThenElse *op) {
        if (branch_count < branching_limit) {
            in_if_cond = true;
            ++branch_count;
            then_case.push(op->then_case);
            else_case.push(op->else_case);
            Expr cond = mutate(op->condition);
            in_if_cond = false;

            stmt = IfThenElse::make(cond, mutate(then_case.top()), mutate(else_case.top()));
            then_case.pop();
            else_case.pop();
            --branch_count;

            if (!cond.same_as(op->condition)) {
                stmt = mutate(stmt);
            }
        } else {
            stmt = op;
        }
    }

    void visit(const Select *op) {
        if (!in_if_cond && !in_select_cond && (branch_count < branching_limit)) {
            bool old_in_select_cond = in_select_cond;
            in_select_cond = true;
            ++branch_count;
            true_value.push(op->true_value);
            false_value.push(op->false_value);
            Expr cond = mutate(op->condition);
            in_select_cond = false;
            expr = Select::make(cond, mutate(true_value.top()), mutate(false_value.top()));
            in_select_cond = old_in_select_cond;
            true_value.pop();
            false_value.pop();
            --branch_count;
            if (!cond.same_as(op->condition)) {
                expr = mutate(expr);
            }
        } else {
            expr = op;
        }
    }

    void visit(const Not *op) {
        if (in_if_cond) {
            if (!else_case.top().defined()) {
                else_case.top() = Evaluate::make(0);
            }
            std::swap(then_case.top(), else_case.top());
            expr = mutate(op->a);
        } else if (in_select_cond) {
            std::swap(true_value.top(), false_value.top());
            expr = mutate(op->a);
        } else {
            expr = op;
        }
    }

    void visit(const And *op) {
        if (in_if_cond) {
            then_case.top() = IfThenElse::make(op->b, then_case.top(), else_case.top());
            expr = op->a;
        } else if (in_select_cond) {
            true_value.top() = Select::make(op->b, true_value.top(), false_value.top());
            expr = op->a;
        } else {
            expr = op;
        }
    }

    void visit(const Or *op) {
        if (in_if_cond) {
            else_case.top() = IfThenElse::make(op->b, then_case.top(), else_case.top());
            expr = op->a;
        } else if (in_select_cond) {
            false_value.top() = Select::make(op->b, true_value.top(), false_value.top());
            expr = op->a;
        } else {
            expr = op;
        }
    }

    void visit(const EQ *op) {
        if (in_if_cond) {
            then_case.top() = IfThenElse::make(op->a >= op->b, then_case.top(), else_case.top());
            expr = op->a <= op->b;
        } else if (in_select_cond) {
            true_value.top() = Select::make(op->a >= op->b, true_value.top(), false_value.top());
            expr = op->a <= op->b;
        } else {
            expr = op;
        }
    }

    void visit(const NE *op) {
        if (in_if_cond) {
            else_case.top() = IfThenElse::make(op->a > op->b, then_case.top(), else_case.top());
            expr = op->a < op->b;
        } else if (in_select_cond) {
            false_value.top() = Select::make(op->a > op->b, true_value.top(), false_value.top());
            expr = op->a < op->b;
        } else {
            expr = op;
        }
    }

    void visit(const Call *op)   {expr = op;}
    void visit(const Store *op)  {stmt = op;}

    /* TODO:
       It is currently not clear whether it is useful to dive into bound boolean valued variables
       inside conditionals, and if so how to do it safely. The code below is commented out in case
       we need to revisit this in the future.
    */
    void visit(const Variable *op) {
        if ((in_if_cond || in_select_cond) &&
            op->type.is_bool() && scope.contains(op->name)) {
            Expr val = scope.get(op->name);
            Expr new_val = mutate(val);
            if (new_val.same_as(val)) {
                expr = op;
            } else {
                expr = new_val;
            }
        } else {
            expr = op;
        }
    }

    void visit(const Let *op) {
        scope.push(op->name, op->value);
        expr = mutate(op->body);
        scope.pop(op->name);

        if (expr.same_as(op->body)) {
            expr = op;
        } else {
            expr = Let::make(op->name, op->value, expr);
        }
    }

    void visit(const LetStmt *op) {
        scope.push(op->name, op->value);
        stmt = mutate(op->body);
        scope.pop(op->name);

        if (stmt.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, op->value, stmt);
        }
    }
};

// Prunes a nested tree of IfThenElse or Select nodes. Uses bounds inference on
// the condition Exprs of these nodes to decide if any internal branches can be
// proven to be true/false and replaces them with the correct case. This is
// intended to be used after we have normalized an IfThenElse Stmt or Select Expr.
//
// NOTE: This mutator is not included as part of the general simplify code, since it
// requires calling the linear solver, which depends on simplify, and thus would
// create a circular dependency. In the future we may be able to change how the
// linear solver and simplifier work together so that this mutator can be merged
// into the simply function.
class PruneBranches : public IRMutator {
public:
    std::string name;
    Scope<Expr> scope;
    Scope<Interval> bounds_info;
    const Scope<int>& free_vars;

    PruneBranches(const std::string &var, const Scope<Expr> *s,
                  const Scope<Interval> *bi, const Scope<int> &v)
            : name(var), free_vars(v) {
        scope.set_containing_scope(s);
        bounds_info.set_containing_scope(bi);
    }

  private:
    using IRMutator::visit;

    // Returns true if the expr is an inequality condition, and
    // returns the intervals when the condition is true and false.
    bool is_inequality(Expr condition, Interval &true_range, Interval &false_range) {
        Expr solve = solve_for_linear_variable(condition, name, free_vars, scope);
        if (!solve.same_as(condition)) {
            Interval var_bounds;
            if (bounds_info.contains(name)) {
                var_bounds = bounds_info.get(name);
            } else {
                var_bounds = Interval(Expr(), Expr());
            }

            bool result = true;
            const LT *lt = solve.as<LT>();
            const LE *le = solve.as<LE>();
            const GT *gt = solve.as<GT>();
            const GE *ge = solve.as<GE>();
            if (lt) {
                true_range.min = var_bounds.min;
                true_range.max = var_bounds.max.defined()? min(lt->b - 1, var_bounds.max): lt->b - 1;

                false_range.min = var_bounds.min.defined()? max(var_bounds.min, lt->b): lt->b;
                false_range.max = var_bounds.max;
            } else if (le) {
                true_range.min = var_bounds.min;
                true_range.max = var_bounds.max.defined()? min(le->b, var_bounds.max): le->b;

                false_range.min = var_bounds.min.defined()? max(var_bounds.min, le->b + 1): le->b + 1;
                false_range.max = var_bounds.max;
            } else if (gt) {
                true_range.min = var_bounds.min.defined()? max(var_bounds.min, gt->b + 1): gt->b + 1;
                true_range.max = var_bounds.max;

                false_range.min = var_bounds.min;
                false_range.max = var_bounds.max.defined()? min(gt->b, var_bounds.max): gt->b;
            } else if (ge) {
                true_range.min = var_bounds.min.defined()? max(var_bounds.min, ge->b): ge->b;
                true_range.max = var_bounds.max;

                false_range.min = var_bounds.min;
                false_range.max = var_bounds.max.defined()? min(ge->b - 1, var_bounds.max): ge->b - 1;
            } else {
                result = false;
            }

            return result;
        }

        return false;
    }

    void visit(const IfThenElse *op) {
        Expr condition = simplify(op->condition, true, bounds_info);
        Stmt then_case = op->then_case;
        Stmt else_case = op->else_case;

        Interval then_range, else_range;
        if (is_inequality(condition, then_range, else_range)) {
            bounds_info.push(name, then_range);
            then_case = mutate(then_case);
            then_case = simplify(then_case, true, bounds_info);
            bounds_info.pop(name);

            bounds_info.push(name, else_range);
            else_case = mutate(else_case);
            else_case = simplify(else_case, true, bounds_info);
            bounds_info.pop(name);
        }

        if (!condition.same_as(op->condition) ||
            !then_case.same_as(op->then_case) ||
            !else_case.same_as(op->else_case)) {
            stmt = IfThenElse::make(condition, then_case, else_case);
        } else {
            then_case = mutate(then_case);
            else_case = mutate(else_case);

            if (!then_case.same_as(op->then_case) || !else_case.same_as(op->else_case)) {
                stmt = IfThenElse::make(condition, then_case, else_case);
            } else {
                stmt = op;
            }
        }
    }

    void visit(const Select *op) {
        Expr condition = simplify(op->condition, true, bounds_info);
        Expr true_value = op->true_value;
        Expr false_value = op->false_value;

        Interval true_range, false_range;
        if (is_inequality(condition, true_range, false_range)) {
            bounds_info.push(name, true_range);
            true_value = mutate(true_value);
            true_value = simplify(true_value, true, bounds_info);
            bounds_info.pop(name);

            bounds_info.push(name, false_range);
            false_value = mutate(false_value);
            false_value = simplify(false_value, true, bounds_info);
            bounds_info.pop(name);
        }

        if (!condition.same_as(op->condition) ||
            !true_value.same_as(op->true_value) ||
            !false_value.same_as(op->false_value)) {
            expr = Select::make(condition, true_value, false_value);
        } else {
            true_value = mutate(true_value);
            false_value = mutate(false_value);

            if (!true_value.same_as(op->true_value) || !false_value.same_as(op->false_value)) {
                expr = Select::make(condition, true_value, false_value);
            } else {
                expr = op;
            }
        }
    }

    void visit(const Let *op) {
        scope.push(op->name, op->value);
        Expr new_body = mutate(op->body);
        scope.pop(op->name);

        if (!new_body.same_as(op->body)) {
            expr = Let::make(op->name, op->value, new_body);
        } else {
            expr = op;
        }
    }

    void visit(const LetStmt *op) {
        scope.push(op->name, op->value);
        Stmt new_body = mutate(op->body);
        scope.pop(op->name);

        if (!new_body.same_as(op->body)) {
            stmt = LetStmt::make(op->name, op->value, new_body);
        } else {
            stmt = op;
        }
    }
};

Stmt normalize_branch_conditions(Stmt stmt, const std::string& var, const Scope<Expr> &scope,
                                 const Scope<Interval> &bounds, const Scope<int> &vars,
                                 const int branching_limit) {
  stmt = NormalizeBranches(&scope, branching_limit).mutate(stmt);
  stmt = PruneBranches(var, &scope, &bounds, vars).mutate(stmt);
  stmt = simplify(stmt, true, bounds);
  return stmt;
}

Expr normalize_branch_conditions(Expr expr, const std::string& var, const Scope<Expr> &scope,
                                 const Scope<Interval> &bounds, const Scope<int> &vars,
                                 const int branching_limit) {
  expr = NormalizeBranches(&scope, branching_limit).mutate(expr);
  expr = PruneBranches(var, &scope, &bounds, vars).mutate(expr);
  expr = simplify(expr, true, bounds);
  return expr;
}

}
}
