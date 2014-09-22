#include <set>

#include "SpecializeClampedRamps.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LinearSolve.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

namespace {

// The generic version of this class doesn't work for our
// purposes. We need to dive into the current scope to decide if a
// variable is used.
class ExprUsesVar : public IRVisitor {
    using IRVisitor::visit;

    std::string name;
    Scope<Expr> scope;

    void visit(const Variable *v) {
        if (v->name == name) {
            result = true;
        } else if (scope.contains(v->name)) {
            scope.get(v->name).accept(this);
        }
    }

    void visit(const Let *op) {
        scope.push(op->name, op->value);
        op->body.accept(this);
        scope.pop(op->name);
    }
  public:
    ExprUsesVar(const std::string &var, const Scope<Expr> *s) :
            name(var), result(false)
    {
        scope.set_containing_scope(s);
    }

    bool result;
};

/** Test if an expression references the given variable. */
bool expr_uses_var(Expr expr, const std::string &var, const Scope<Expr> &scope) {
    ExprUsesVar uses_var(var, &scope);
    expr.accept(&uses_var);
    return uses_var.result;
}

class NormalizeIfStmts : public IRMutator {
public:
    NormalizeIfStmts() {}

private:
    using IRVisitor::visit;

    bool in_if_stmt;
    Stmt then_case;
    Stmt else_case;

    void visit(const IfThenElse *op) {
        in_if_stmt = true;
        then_case = op->then_case;
        else_case = op->else_case;
        Expr cond = mutate(op->condition);
        in_if_stmt = false;
        stmt = IfThenElse::make(cond, mutate(then_case), mutate(else_case));
        if (!cond.same_as(op->condition)) {
            stmt = mutate(stmt);
        }
    }

    void visit(const Not *op) {
        if (in_if_stmt) {
            if (!else_case.defined()) {
                else_case = Evaluate::make(0);
            }
            std::swap(then_case, else_case);
            expr = mutate(op->a);
        } else {
            expr = op;
        }
    }

    void visit(const And *op) {
        if (in_if_stmt) {
            then_case = IfThenElse::make(op->b, then_case, else_case);
            expr = op->a;
        } else {
            expr = op;
        }
    }

    void visit(const Or *op) {
        if (in_if_stmt) {
            else_case = IfThenElse::make(op->b, then_case, else_case);
            expr = op->a;
        } else {
            expr = op;
        }
    }

    void visit(const Store *op) {
        stmt = op;
    }
};

Stmt normalize_if_stmts(Stmt stmt) {
    return NormalizeIfStmts().mutate(stmt);
}

}

struct Branch {
    Expr min;
    Expr extent;
    Expr expr;
    Stmt stmt;
};

class BranchesInVar : public IRVisitor {
public:
    std::string name;
    Scope<Expr> scope;
    bool has_branches;
    bool branch_on_minmax;

    BranchesInVar(const std::string& n, const Scope<Expr> *s, bool minmax) :
            name(n), has_branches(false), branch_on_minmax(minmax)
    {
        scope.set_containing_scope(s);
    }

private:
    using IRVisitor::visit;

    void visit(const IfThenElse *op) {
        if (expr_uses_var(op->condition, name, scope)) {
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Select *op) {
        if (expr_uses_var(op->condition, name, scope) &&
            op->condition.type().is_scalar() &&
            op->true_value.type().is_vector()) {
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Min *op) {
        if (branch_on_minmax &&
            (expr_uses_var(op->a, name, scope) ||
             expr_uses_var(op->b, name, scope))) {
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Max *op) {
        if (branch_on_minmax &&
            (expr_uses_var(op->a, name, scope) ||
             expr_uses_var(op->b, name, scope))) {
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    template<class LetOp>
    void visit_let(const LetOp *op) {
        scope.push(op->name, op->value);
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const LetStmt *op) {visit_let(op);}
    void visit(const Let *op) {visit_let(op);}
};

bool stmt_branches_in_var(const std::string &name, Stmt body, const Scope<Expr> &scope,
                          bool branch_on_minmax = false) {
    BranchesInVar check(name, &scope, branch_on_minmax);
    body.accept(&check);
    return check.has_branches;
}

bool expr_branches_in_var(const std::string &name, Expr value, const Scope<Expr> &scope,
                          bool branch_on_minmax = false) {
    BranchesInVar check(name, &scope, branch_on_minmax);
    value.accept(&check);
    return check.has_branches;
}

class FindFreeVariables : public IRVisitor {
public:
    const Scope<int> &free_vars;
    std::set<std::string> vars;

    FindFreeVariables(const Scope<int> &fv) : free_vars(fv) {}
private:
    using IRVisitor::visit;

    void visit(const Variable *op) {
        if (free_vars.contains(op->name)) {
            vars.insert(op->name);
        }
    }
};

size_t num_free_vars(Expr expr, const Scope<int> &free_vars) {
    FindFreeVariables find(free_vars);
    expr.accept(&find);
    return find.vars.size();
}

bool has_free_vars(Expr expr, const Scope<int> &free_vars) {
    return num_free_vars(expr, free_vars) > 0;
}

void collect_branches(Expr expr, const std::string& name, Expr min, Expr extent,
                      std::vector<Branch> &branches, const Scope<Expr> &scope,
                      const Scope<int> &free_vars);
void collect_branches(Stmt stmt, const std::string& name, Expr min, Expr extent,
                      std::vector<Branch> &branches, const Scope<Expr> &scope,
                      const Scope<int> &free_vars);

class BranchCollector : public IRVisitor {
public:
    std::string name;
    std::vector<Branch> branches;
    Scope<Expr> scope;
    const Scope<int> &free_vars;
    Expr min;
    Expr extent;

    BranchCollector(const std::string &n, Expr m, Expr e, const Scope<Expr> *s,
                    const Scope<int> &lv) :
        name(n), free_vars(lv), min(m), extent(e)
    {
        scope.set_containing_scope(s);
    }

private:
    using IRVisitor::visit;

    std::vector< std::string > internal_lets;

    // Build a pair of branches for 2 exprs, based on a simple inequality conditional.
    // It is assumed that the inequality has been solved and so the variable of interest
    // is on the left hand side.
    void build_branches(Expr cond, Expr a, Expr b, Branch &b1, Branch &b2) {
        const LE *le = cond.as<LE>();
        const GE *ge = cond.as<GE>();
        const LT *lt = cond.as<LT>();
        const GT *gt = cond.as<GT>();

        if (le) {
            Expr ext1 = simplify(le->b - min + 1);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.expr = a;

            b2.min = simplify(le->b + 1);
            b2.extent = ext2;
            b2.expr = b;
        } else if(lt) {
            Expr ext1 = simplify(lt->b - min);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.expr = a;

            b2.min = lt->b;
            b2.extent = ext2;
            b2.expr = b;
        } else if(ge) {
            Expr ext1 = simplify(ge->b - min + 1);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.expr = b;

            b2.min = simplify(ge->b + 1);
            b2.extent = ext2;
            b2.expr = a;
        } else if(gt) {
            Expr ext1 = simplify(gt->b - min);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.expr = b;

            b2.min = gt->b;
            b2.extent = ext2;
            b2.expr = a;
        }
    }

    // Build a pair of branches for 2 stmts, based on a simple inequality conditional.
    // It is assumed that the inequality has been solved and so the variable of interest
    // is on the left hand side.
    void build_branches(Expr cond, Stmt a, Stmt b, Branch &b1, Branch &b2) {
        const LE *le = cond.as<LE>();
        const GE *ge = cond.as<GE>();
        const LT *lt = cond.as<LT>();
        const GT *gt = cond.as<GT>();

        if (le) {
            Expr ext1 = simplify(le->b - min + 1);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.stmt = a;

            b2.min = simplify(le->b + 1);
            b2.extent = ext2;
            b2.stmt = b;
        } else if(lt) {
            Expr ext1 = simplify(lt->b - min);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.stmt = a;

            b2.min = lt->b;
            b2.extent = ext2;
            b2.stmt = b;
        } else if(ge) {
            Expr ext1 = simplify(ge->b - min + 1);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.stmt = b;

            b2.min = simplify(ge->b + 1);
            b2.extent = ext2;
            b2.stmt = a;
        } else if(gt) {
            Expr ext1 = simplify(gt->b - min);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.stmt = b;

            b2.min = gt->b;
            b2.extent = ext2;
            b2.stmt = a;
        }
    }

    Stmt wrap_lets(Stmt stmt) {
        std::vector<std::string>::reverse_iterator iter = internal_lets.rbegin();
        while (iter != internal_lets.rend()) {
            const std::string& name = *iter++;
            stmt = LetStmt::make(name, scope.get(name), stmt);
        }
        return stmt;
    }

    void visit_simple_cond(Expr cond, Expr a, Expr b) {
        // Bail out if this condition depends on more than just the current loop variable.
        if (num_free_vars(cond, free_vars) > 1) return;

        Expr solve = solve_for_linear_variable(cond, name, scope);
        if (!solve.same_as(cond)) {
            Branch b1, b2;
            build_branches(solve, a, b, b1, b2);

            size_t num_branches = branches.size();
            Expr orig_min = min;
            Expr orig_extent = extent;

            min = b1.min;
            extent = b1.extent;
            b1.expr.accept(this);

            // If we didn't branch any further, push these branches onto the stack.
            if (branches.size() == num_branches) {
                branches.push_back(b1);
            }
            num_branches = branches.size();

            min = b2.min;
            extent = b2.extent;
            b2.expr.accept(this);

            // If we didn't branch any further, push these branches onto the stack.
            if (branches.size() == num_branches) {
                branches.push_back(b2);
            }

            min = orig_min;
            extent = orig_extent;
        }
    }

    template<class Op, class Cmp>
    void visit_minormax(const Op *op) {
        Expr a = op->a;
        Expr b = op->b;

        if (expr_uses_var(a, name, scope) || expr_uses_var(b, name, scope)) {
            Expr cond = Cmp::make(a, b);
            visit_simple_cond(cond, a, b);
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Min *op) {visit_minormax<Min, LE>(op);}
    void visit(const Max *op) {visit_minormax<Max, GE>(op);}

    void visit(const Select *op) {
        if (expr_uses_var(op->condition, name, scope) &&
            op->condition.type().is_scalar() &&
            op->true_value.type().is_vector()) {
            visit_simple_cond(op->condition, op->true_value, op->false_value);
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        if (expr_branches_in_var(name, op->value, scope, true)) {
            std::vector<Branch> expr_branches;
            collect_branches(op->value, name, min, extent, expr_branches, scope, free_vars);

            size_t num_branches = branches.size();
            Expr orig_min = min;
            Expr orig_extent = extent;

            for (size_t i = 0; i < expr_branches.size(); ++i) {
                Branch &branch = expr_branches[i];
                branch.stmt = substitute(op->name, branch.expr, op->body);
                branch.expr = Expr();

                min = branch.min;
                extent = branch.extent;
                branch.stmt.accept(this);

                if (branches.size() == num_branches) {
                    branch.stmt = wrap_lets(branch.stmt);
                    branches.push_back(branch);
                }
                num_branches = branches.size();
            }

            min = orig_min;
            extent = orig_extent;
        } else {
            internal_lets.push_back(op->name);
            scope.push(op->name, op->value);
            op->body.accept(this);
            scope.pop(op->name);
            internal_lets.pop_back();
        }
    }

    void visit(const Store *op) {
        if (expr_branches_in_var(name, op->value, scope, true)) {
            std::vector<Branch> expr_branches;
            collect_branches(op->value, name, min, extent, expr_branches, scope, free_vars);

            for (size_t i = 0; i < expr_branches.size(); ++i) {
                Branch &branch = expr_branches[i];
                branch.stmt = wrap_lets(Store::make(op->name, branch.expr, op->index));
                branch.expr = Expr();
                branches.push_back(branch);
            }
        }
    }

    void visit(const IfThenElse *op) {
        bool has_else = op->else_case.defined();
        if (expr_uses_var(op->condition, name, scope)) {
            Stmt normalized = normalize_if_stmts(op);
            const IfThenElse *if_stmt = normalized.as<IfThenElse>();

            // Bail out if this condition depends on more than just the current loop variable.
            if (num_free_vars(if_stmt->condition, free_vars) > 1) return;

            Expr solve = solve_for_linear_variable(if_stmt->condition, name, scope);
            if (!solve.same_as(if_stmt->condition)) {
                Branch b1, b2;
                build_branches(solve, if_stmt->then_case, if_stmt->else_case, b1, b2);

                size_t num_branches = branches.size();
                Expr orig_min = min;
                Expr orig_extent = extent;

                min = b1.min;
                extent = b1.extent;
                b1.stmt.accept(this);

                // If we didn't branch any further, push this branches onto the stack.
                if (branches.size() == num_branches) {
                    b1.stmt = wrap_lets(b1.stmt);
                    branches.push_back(b1);
                }

                if (has_else) {
                    num_branches = branches.size();

                    min = b2.min;
                    extent = b2.extent;
                    b2.stmt.accept(this);

                    // If we didn't branch any further, push this branches onto the stack.
                    if (branches.size() == num_branches) {
                        b2.stmt = wrap_lets(b2.stmt);
                        branches.push_back(b2);
                    }
                }

                min = orig_min;
                extent = orig_extent;
            }
        } else {
            op->then_case.accept(this);
            if (has_else) {
                op->else_case.accept(this);
            }
        }
    }

};

void collect_branches(Expr expr, const std::string& name, Expr min, Expr extent,
                      std::vector<Branch> &branches, const Scope<Expr> &scope,
                      const Scope<int> &free_vars) {
    BranchCollector collector(name, min, extent, &scope, free_vars);
    expr.accept(&collector);
    branches.swap(collector.branches);
}

void collect_branches(Stmt stmt, const std::string& name, Expr min, Expr extent,
                      std::vector<Branch> &branches, const Scope<Expr> &scope,
                      const Scope<int> &free_vars) {
    BranchCollector collector(name, min, extent, &scope, free_vars);
    stmt.accept(&collector);
    branches.swap(collector.branches);
}

class SpecializeBranchedLoops : public IRMutator {
private:
    using IRVisitor::visit;

    Scope<Expr> scope;
    Scope<int>  loop_vars;

    void visit(const For *op) {
        loop_vars.push(op->name, 0);
        Stmt body = mutate(op->body);

        if (op->for_type == For::Serial && stmt_branches_in_var(op->name, body, scope)) {
            std::vector<Branch> branches;
            collect_branches(body, op->name, op->min, op->extent, branches, scope, loop_vars);

            if (branches.empty()) {
                stmt = For::make(op->name, op->min, op->extent, op->for_type, body);
            } else {
                Scope<Interval> bounds;
                stmt = Evaluate::make(0);
                for (int i = branches.size()-1; i >= 0; --i) {
                    Branch &branch = branches[i];
                    bounds.push(op->name, Interval(branch.min, branch.min + branch.extent - 1));
                    Expr extent = simplify(branch.extent, true, bounds);
                    if (is_zero(extent)) continue;
                    Stmt branch_stmt = simplify(branch.stmt, true, bounds);
                    branch_stmt = For::make(op->name, branch.min, extent, op->for_type, branch_stmt);
                    stmt = Block::make(branch_stmt, stmt);
                    bounds.pop(op->name);
                }
            }
        } else {
            stmt = For::make(op->name, op->min, op->extent, op->for_type, body);
        }
        loop_vars.pop(op->name);
    }

    void visit(const LetStmt *op) {
        scope.push(op->name, op->value);
        stmt = LetStmt::make(op->name, op->value, mutate(op->body));
        scope.pop(op->name);
    }
};

Stmt specialize_branched_loops(Stmt s) {
    return SpecializeBranchedLoops().mutate(s);
}

}
}
