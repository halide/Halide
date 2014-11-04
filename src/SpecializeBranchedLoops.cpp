#include <queue>
#include <vector>

#include "SpecializeBranchedLoops.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LinearSolve.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

namespace {

// A compile time variable that limits that maximum number of branches
// that we generate in this optimization pass. This prevents a
// combinatorial explosion of code generation.
static const int branching_limit = 10;

// A variant type that encapsulates a Stmt or Expr handle.
class StmtOrExpr {
public:
    StmtOrExpr() {}
    StmtOrExpr(Stmt s) : stmt(s) {}
    StmtOrExpr(Expr e) : expr(e) {}

    bool is_stmt() const {return stmt.defined();}
    bool is_expr() const {return expr.defined();}
    bool defined() const {return is_stmt() || is_expr();}

    void accept(IRVisitor *visit) {
        if (is_stmt()) {
            stmt.accept(visit);
        } else {
            expr.accept(visit);
        }
    }

    StmtOrExpr operator= (Stmt s) {stmt = s; expr = Expr(); return *this;}
    StmtOrExpr operator= (Expr e) {stmt = Stmt(); expr = e; return *this;}

    operator Stmt() const {return stmt;}
    operator Expr() const {return expr;}
private:
    Stmt stmt;
    Expr expr;
};

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

/* Test if an expression references the given variable. */
bool expr_uses_var(Expr expr, const std::string &var, const Scope<Expr> &scope) {
    ExprUsesVar uses_var(var, &scope);
    expr.accept(&uses_var);
    return uses_var.result;
}

/* This mutator is used as a second pass after NormalizeIfStmts as a
 * means to reduce the number of intermediate steps thet the
 * BranchCollector needs to perform. The goal here is to detect
 * tautological conditions inside internal if stmts. When we find an
 * IfThenElse node we try to prove that the condition is true or
 * false, if we can do that succesfully, then we can replace the
 * entire node by either the then or else case respectively.
 */
class PruneIfStmtTree : public IRMutator {
public:
    std::string name;
    Scope<Expr> scope;
    Scope<Interval> bounds_info;
    const Scope<int>& free_vars;

    PruneIfStmtTree(const std::string &var, const Scope<Expr> *s,
                    const Scope<Interval> *bounds, const Scope<int> &vars)
            : name(var), free_vars(vars) {
        scope.set_containing_scope(s);
        bounds_info.set_containing_scope(bounds);
    }

  private:
    using IRMutator::visit;

    void visit(const IfThenElse *op) {
        Expr condition = simplify(op->condition, true, bounds_info);
        Stmt then_case = op->then_case;
        Stmt else_case = op->else_case;

        // debug(0) << "Pruning:\n" << Stmt(op);

        Expr solve = solve_for_linear_variable(condition, name, free_vars);
        if (!solve.same_as(condition)) {
            Interval var_bounds;
            Expr min_then, max_then;
            Expr min_else, max_else;

            if (bounds_info.contains(name)) {
                var_bounds = bounds_info.get(name);
            } else {
                var_bounds = Interval(Expr(), Expr());
            }

            const LT *lt = solve.as<LT>();
            const LE *le = solve.as<LE>();
            const GT *gt = solve.as<GT>();
            const GE *ge = solve.as<GE>();
            if (lt) {
                min_then = var_bounds.min;
                max_then = var_bounds.max.defined()? min(lt->b - 1, var_bounds.max): lt->b - 1;

                min_else = var_bounds.min.defined()? max(var_bounds.min, lt->b): lt->b;
                max_else = var_bounds.max;
            } else if (le) {
                min_then = var_bounds.min;
                max_then = var_bounds.max.defined()? min(le->b, var_bounds.max): le->b;

                min_else = var_bounds.min.defined()? max(var_bounds.min, le->b + 1): le->b + 1;
                max_else = var_bounds.max;
            } else if (gt) {
                min_then = var_bounds.min.defined()? max(var_bounds.min, gt->b + 1): gt->b + 1;
                max_then = var_bounds.max;

                min_else = var_bounds.min;
                max_else = var_bounds.max.defined()? min(gt->b, var_bounds.max): gt->b;
            } else if (ge) {
                min_then = var_bounds.min.defined()? max(var_bounds.min, ge->b): ge->b;
                max_then = var_bounds.max;

                min_else = var_bounds.min;
                max_else = var_bounds.max.defined()? min(ge->b - 1, var_bounds.max): ge->b - 1;
            }

            bounds_info.push(name, Interval(min_then, max_then));
            // debug(0) << "Mutating then case:\n" << then_case << "\n";
            then_case = mutate(then_case);
            then_case = simplify(then_case, true, bounds_info);
            bounds_info.pop(name);

            bounds_info.push(name, Interval(min_else, max_else));
            // debug(0) << "Mutating else case:\n" << else_case << "\n";
            else_case = mutate(else_case);
            else_case = simplify(else_case, true, bounds_info);
            bounds_info.pop(name);
        }

        if (!condition.same_as(op->condition) ||
            !then_case.same_as(op->then_case) ||
            !else_case.same_as(op->else_case)) {
            condition = solve;
            stmt = IfThenElse::make(condition, then_case, else_case);
        } else {
            // We didn't mutate the stmt as is, but we can still try to prune stmts of the form:
            //
            //   if (x < a) {
            //       if (x < b) {
            //         ...
            //       } else {
            //         ...
            //       }
            //   }
            //
            // by transforming them into:
            //
            //   if (x < b) {
            //       if (x < a) {
            //        ...
            //       }
            //   } else {
            //       if (x < a) {
            //         ...
            //       }
            //   }
            //
            // and mutating the transformed stmt.
            const IfThenElse *inner_if = then_case.as<IfThenElse>();
            if (inner_if && !else_case.defined()) {
                const IfThenElse *inner_if = then_case.as<IfThenElse>();
                Stmt new_then = IfThenElse::make(condition, inner_if->then_case);
                Stmt new_else = inner_if->else_case.defined()?
                    IfThenElse::make(condition, inner_if->then_case) : else_case;
                Stmt new_if = IfThenElse::make(inner_if->condition, new_then, new_else);
                Stmt new_stmt = mutate(new_if);

                if (!new_stmt.same_as(new_if)) {
                    stmt = new_stmt;
                    return;
                }
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

        // debug(0) << "Prunded stmt:\n" << stmt << "\n\n";
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

Stmt prune_if_stmt_tree(Stmt s, const std::string &var, const Scope<Expr> &scope,
                        const Scope<Interval> &bounds, const Scope<int> &vars) {
    PruneIfStmtTree pruner(var, &scope, &bounds, vars);
    Stmt pruned = simplify(pruner.mutate(s), true, bounds);
    // while (!pruned.same_as(s)) {
    //     s = pruned;
    //     pruned = simplify(pruner.mutate(s), true, bounds);
    // }
    return pruned;
}

/* This mutator transforms a Stmt so that the condition of all the
 * IfThenElse nodes are "simple". i.e. we remove all of the logical
 * connectives that may appear in a conditional. Additionally, we
 * transform EQ and NE nodes into compound expressions involve LE & GE
 * and LT & GT respectively. This allows the BranchCollector to only
 * solve conditions involving single inequalities.
 */
class NormalizeIfStmts : public IRMutator {
public:
    NormalizeIfStmts(const Scope<Expr> *s) : in_if_stmt(false), branch_count(0) {
        scope.set_containing_scope(s);
    }

private:
    using IRMutator::visit;

    Scope<Expr> scope;
    bool in_if_stmt;
    int branch_count;
    std::stack<Stmt> then_case;
    std::stack<Stmt> else_case;

    void visit(const IfThenElse *op) {
        if (branch_count < branching_limit) {
            in_if_stmt = true;
            ++branch_count;
            then_case.push(op->then_case);
            else_case.push(op->else_case);
            Expr cond = mutate(op->condition);
            in_if_stmt = false;
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

    void visit(const Not *op) {
        if (in_if_stmt) {
            if (!else_case.top().defined()) {
                else_case.top() = Evaluate::make(0);
            }
            std::swap(then_case.top(), else_case.top());
            expr = mutate(op->a);
        } else {
            expr = op;
        }
    }

    void visit(const And *op) {
        if (in_if_stmt) {
            then_case.top() = IfThenElse::make(op->b, then_case.top(), else_case.top());
            expr = op->a;
        } else {
            expr = op;
        }
    }

    void visit(const Or *op) {
        if (in_if_stmt) {
            else_case.top() = IfThenElse::make(op->b, then_case.top(), else_case.top());
            expr = op->a;
        } else {
            expr = op;
        }
    }

    void visit(const EQ *op) {
        if (in_if_stmt) {
            then_case.top() = IfThenElse::make(op->a >= op->b, then_case.top(), else_case.top());
            expr = op->a <= op->b;
        } else {
            expr = op;
        }
    }

    void visit(const NE *op) {
        if (in_if_stmt) {
            else_case.top() = IfThenElse::make(op->a > op->b, then_case.top(), else_case.top());
            expr = op->a < op->b;
        } else {
            expr = op;
        }
    }

    void visit(const Call *op)   {expr = op;}
    void visit(const Select *op) {expr = op;}
    void visit(const Store *op)  {stmt = op;}

    void visit(const Variable *op) {
        if (in_if_stmt && op->type.is_bool() && scope.contains(op->name)) {
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

Stmt normalize_if_stmts(Stmt stmt, const Scope<Expr> &scope) {
    return simplify(NormalizeIfStmts(&scope).mutate(stmt));
}

/* This mutator pefroms the same transformation as NormalizeIfStmts,
 * except this transforms select exprs. See the description in
 * NormalizeIfStmts for more details.
 */
class NormalizeSelect : public IRMutator {
  public:
    NormalizeSelect(const Scope<Expr> *s) : in_select_cond(false), branch_count(0) {
        scope.set_containing_scope(s);
    }

  private:
    using IRVisitor::visit;

    Scope<Expr> scope;
    bool in_select_cond;
    int branch_count;
    std::stack<Expr> true_value;
    std::stack<Expr> false_value;

    void visit(const Select *op) {
        if (branch_count < branching_limit) {
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
        if (in_select_cond) {
            std::swap(true_value.top(), false_value.top());
            expr = mutate(op->a);
        } else {
            expr = op;
        }
    }

    void visit(const And *op) {
        if (in_select_cond) {
            true_value.top() = Select::make(op->b, true_value.top(), false_value.top());
            expr = op->a;
        } else {
            expr = op;
        }
    }

    void visit(const Or *op) {
        if (in_select_cond) {
            false_value.top() = Select::make(op->b, true_value.top(), false_value.top());
            expr = op->a;
        } else {
            expr = op;
        }
    }

    void visit(const EQ *op) {
        if (in_select_cond) {
            true_value.top() = Select::make(op->a >= op->b, true_value.top(), false_value.top());
            expr = op->a <= op->b;
        } else {
            expr = op;
        }
    }

    void visit(const NE *op) {
        if (in_select_cond) {
            false_value.top() = Select::make(op->a > op->b, true_value.top(), false_value.top());
            expr = op->a < op->b;
        } else {
            expr = op;
        }
    }

    void visit(const Call *op)   {expr = op;}

    void visit(const Variable *op) {
        if (in_select_cond && op->type.is_bool() && scope.contains(op->name)) {
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

Expr normalize_select(Expr expr, const Scope<Expr> &scope) {
    return NormalizeSelect(&scope).mutate(expr);
}

}

struct Branch {
    Expr min;
    Expr extent;
    Expr expr;
    Stmt stmt;
};

bool operator< (const Branch &b1, const Branch &b2) {
    static IRDeepCompare compare;
    if (equal(b1.min, b2.min)) {
        return compare(b1.extent, b2.extent);
    } else {
        return compare(b1.min, b2.min);
    }
}

std::ostream &operator<<(std::ostream &out, const Branch &b) {
    out << "branch(" << b.min << ", " << b.extent << "): {";
    if (b.expr.defined()) {
        out << b.expr;
    }
    if (b.stmt.defined()) {
        out << "\n" << b.stmt;
    }
    out << "}";
    return out;
}

/* A simple visitor that checks if a given Stmt or Expr has any
 * branches in a particular variable. The flag branch_on_minmax
 * specifies whether or not we should consider min/max nodes as
 * branches. This is useful for not introducing branches when the only
 * source is min/max exprs.
 */
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
            op->condition.type().is_scalar()) {
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
        op->value.accept(this);
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

/* Simple visitor that checks how many variables in a stmt or expr
 * appear in a fixed list of "free" variables.
 */
class FindFreeVariables : public IRGraphVisitor {
  public:
    const Scope<Expr> &scope;
    const Scope<int>  &free_vars;
    std::set<std::string> vars;

    FindFreeVariables(const Scope<Expr> &s, const Scope<int> &fv) : scope(s), free_vars(fv) {}
  private:
    using IRVisitor::visit;

    void visit(const Variable *op) {
        if (free_vars.contains(op->name)) {
            vars.insert(op->name);
        } else if (scope.contains(op->name)) {
            include(scope.get(op->name));
        }
    }
};

size_t num_free_vars(Expr expr, const Scope<Expr> &scope, const Scope<int> &free_vars) {
    FindFreeVariables find(scope, free_vars);
    expr.accept(&find);
    return find.vars.size();
}

bool has_free_vars(Expr expr, const Scope<Expr> &scope, const Scope<int> &free_vars) {
    return num_free_vars(expr, scope, free_vars) > 0;
}

/* This visitor performs the main work for the
 * specialize_branched_loops optimization pass. We are given a
 * variable that we are checking for branches in, a current scope, a
 * list of any other free variables, and some bounds infor on those
 * variables. This visitor then applies to a Stmt in the branch
 * variable and attempts to build up a list of Branch structs, which
 * define the bounds and contents of each branch of the loop.
 */
class BranchCollector : public IRVisitor {
public:
    std::string name;
    std::vector<Branch> branches;
    Scope<Expr> scope;
    Scope<int> free_vars;
    Scope<Interval> bounds_info;
    Expr min;
    Expr extent;

    BranchCollector(const std::string &n, Expr m, Expr e, const Scope<Expr> *s,
                    const Scope<int> *lv, const Scope<Interval> *bi) :
            name(n), min(m), extent(e)
    {
        scope.set_containing_scope(s);
        free_vars.set_containing_scope(lv);
        bounds_info.set_containing_scope(bi);
    }

    Stmt construct_stmt() {
        Stmt stmt;
        bool first = true;
        std::queue<std::string> bounds_vars;
        for (int i = branches.size()-1; i >= 0; --i) {
            Branch &b = branches[i];
            Expr b_min = b.min;
            Expr b_extent = b.extent;

            bounds_info.push(name, Interval(b_min, b_min + b_extent - 1));

            // First, we replace the min/extent exprs in the branch by
            // unique variables, pushing the corresponding exprs onto the
            // branch_scope. Before actually adding the branch to the list
            // of branches.
            std::ostringstream branch_name;
            branch_name << name << ".b" << i;
            if (should_extract(extent)) {
                std::string extent_name = branch_name.str() + ".extent";
                bounds_vars.push(extent_name);
                scope.push(extent_name, b_extent);
                b_extent = Variable::make(b_extent.type(), extent_name);
            }

            if (should_extract(min)) {
                std::string min_name = branch_name.str() + ".min";
                bounds_vars.push(min_name);
                scope.push(min_name, b_min);
                b_min = Variable::make(b_min.type(), min_name);
            }

            Stmt branch_stmt = simplify(b.stmt, true, bounds_info);
            branch_stmt = For::make(name, b_min, b_extent, For::Serial, branch_stmt);
            if (first) {
                stmt = branch_stmt;
                first = false;
            } else {
                stmt = Block::make(branch_stmt, stmt);
            }

            bounds_info.pop(name);
        }

        while (!bounds_vars.empty()) {
            const std::string &var = bounds_vars.front();
            stmt = LetStmt::make(var, scope.get(var), stmt);
            scope.pop(var);
            bounds_vars.pop();
        }

        return stmt;
    }

  private:
    using IRVisitor::visit;

    void print_branches() {
        std::cout << "Branch collector has branched loop " << name << " into:\n";
        for (size_t i = 0; i < branches.size(); ++i) {
            std::cout << "\t" << branches[i];
        }
        std::cout << "\n";
    }

    void get_branch_content(const Branch &b, StmtOrExpr &stmt_or_expr) {
        if (b.expr.defined()) {
            stmt_or_expr = b.expr;
        } else {
            stmt_or_expr = b.stmt;
        }
    }

    // A method used to test if we should pull out a min/extent expr
    // into a let stmt, modified from CSE.cpp
    bool should_extract(Expr e) {
        if (is_const(e)) {
            return false;
        }

        if (e.as<Variable>()) {
            return false;
        }

        if (const Cast *a = e.as<Cast>()) {
            return should_extract(a->value);
        }

        if (const Add *a = e.as<Add>()) {
            return !(is_const(a->a) || is_const(a->b));
        }

        if (const Sub *a = e.as<Sub>()) {
            return !(is_const(a->a) || is_const(a->b));
        }

        if (const Mul *a = e.as<Mul>()) {
            return !(is_const(a->a) || is_const(a->b));
        }

        if (const Div *a = e.as<Div>()) {
            return !(is_const(a->a) || is_const(a->b));
        }

        return true;
    }

    // template<class StmtOrExpr>
    Branch make_branch(Expr min, Expr extent, StmtOrExpr content) {
        Branch b;
        b.min = min;
        b.extent = extent;
        if (content.is_stmt()) {
            b.stmt = content;
        } else {
            b.expr = content;
        }

        // debug(0) << "Making branch: " << b << "\n";

        return b;
    }

    // Try to prove an expr can be reduced to a constant true or false value.
    bool can_prove_expr(Expr expr, bool &is_true) {
        expr = simplify(expr, true, bounds_info);
        if (is_zero(expr)) {
            is_true = false;
            return true;
        } else if (is_one(expr)) {
            is_true = true;
            return true;
        }

        return false;
    }

    // Try to prove if the point is in the given range. Return true if
    // we can proved this, and set in_range to the proved
    // value. Otherwise, we return false if this can't be proved.
    bool can_prove_in_range(Expr point, Expr min, Expr extent, bool &in_range) {
        Expr test = point >= min && point < min + extent;
        return can_prove_expr(test, in_range);
    }

    Expr branch_point(Expr cond, bool &swap) {
        const LE *le = cond.as<LE>();
        const GE *ge = cond.as<GE>();
        const LT *lt = cond.as<LT>();
        const GT *gt = cond.as<GT>();

        swap = false;
        if (le) {
            if (is_zero(le->a)) {
                if (is_zero(le->b) || is_positive_const(le->b)) {
                    return simplify(min + extent - 1);
                } else {
                    return min;
                }
            } else {
                return simplify(le->b + 1);
            }
        } else if (lt) {
            if (is_zero(lt->a)) {
                if (is_positive_const(lt->b)) {
                    return simplify(min + extent - 1);
                } else {
                    return min;
                }
            } else {
                return simplify(lt->b);
            }
        } else if (ge) {
            if (is_zero(ge->a)) {
                if (is_zero(ge->b) || is_negative_const(ge->b)) {
                    return simplify(min + extent - 1);
                } else {
                    return min;
                }
            } else {
                swap = true;
                return simplify(ge->b);
            }
        } else if (gt) {
            if (is_zero(gt->a)) {
                if (is_negative_const(gt->b)) {
                    return simplify(min + extent);
                } else {
                    return min;
                }
            } else {
                swap = true;
                return simplify(gt->b + 1);
            }
        }

        return Expr();
    }

    // Build a pair of branches for 2 exprs, based on a simple inequality conditional.
    // It is assumed that the inequality has been solved and so the variable of interest
    // is on the left hand side.
    // template<class StmtOrExpr>
    bool build_branches(Expr cond, StmtOrExpr a, StmtOrExpr b, Branch &b1, Branch &b2) {
        Expr max = simplify(min + extent - 1);
        bounds_info.push(name, Interval(min, max));

        // debug(0) << "Branching in range [" << min << ", " << max << "] on condition " << cond << ":\n";

        Expr min1, min2;
        Expr ext1, ext2;

        bool swap;
        bool in_range;
        Expr point = branch_point(cond, swap);
        // debug(0) << "\tBranch point @ " << point << "\n";
        if (can_prove_in_range(point, min, extent, in_range)) {
            if (in_range) {
                min1 = min;
                min2 = point;
                ext1 = simplify(point - min);
                ext2 = simplify(min + extent - point);
            } else {
                Expr below_range = simplify(point < min, true, bounds_info);
                if (is_one(below_range)) {
                    min1 = min2 = min;
                    ext1 = 0;
                    ext2 = extent;
                } else {
                    min1 = min;
                    min2 = max;
                    ext1 = extent;
                    ext2 = 0;
                }
            }
        } else {
            min1 = min;
            ext1 = simplify(clamp(point - min, 0, extent));
            min2 = simplify(min + ext1);
            ext2 = simplify(extent - ext1);
        }

        if (swap) {
            std::swap(a, b);
        }

        b1 = make_branch(min1, ext1, a);
        b2 = make_branch(min2, ext2, b);

        bounds_info.pop(name);
        return true;
    }

    // This function generalizes how we must visit all expr or stmt nodes with child nodes.
    template<class Op> //, class StmtOrExpr>
    void branch_children(const Op *op, const std::vector<StmtOrExpr>& children) {
        std::vector<Expr> branch_points;
        std::vector<std::vector<StmtOrExpr> > args;

        Expr max = simplify(min + extent);
        branch_points.push_back(min);
        branch_points.push_back(max);
        args.push_back(std::vector<StmtOrExpr>());

        for (size_t i = 0; i < children.size(); ++i) {
            StmtOrExpr child = children[i];

            size_t old_num_branches = branches.size();
            if (child.defined()) {
                child.accept(this);
            }

            if (branches.size() == old_num_branches) {
                // Child stmt or expr did not branch, thus we add the
                // child to the args list for each branch directly.
                for (size_t j = 0; j < args.size(); ++j) {
                    args[j].push_back(child);
                }
            } else {
                // Pull the child branches created by the collector out of the branch list.
                std::vector<Branch> child_branches(branches.begin() + old_num_branches, branches.end());
                branches.erase(branches.begin() + old_num_branches, branches.end());

                // debug(0) << "Branching child (" << child << "):\n"
                //          << "  Current branch points " << branch_points.size() << " {";
                // for (size_t j = 0; j < branch_points.size(); ++j) {
                //   debug(0) << branch_points[j] << (j < branch_points.size()-1? ", ": "}\n");
                // }
                // debug(0) << "  Child branch points   " << child_branches.size() <<  " {";
                // for (size_t j = 0; j < child_branches.size(); ++j) {
                //   debug(0) << child_branches[j].min << ", ";
                // }
                // debug(0) << branch_points.back() << "}\n";

                if (args.size() == 1) {
                    // This is the first child with branches, we can simply insert all the
                    // branch points from the child branches into the branch_points array.
                    std::vector<StmtOrExpr> old_args = args[0];

                    StmtOrExpr arg;
                    get_branch_content(child_branches[0], arg);
                    args[0].push_back(arg);
                    Expr max = branch_points.back();
                    branch_points.pop_back();
                    for (size_t j = 1; j < child_branches.size(); ++j) {
                        Branch &b = child_branches[j];
                        get_branch_content(b, arg);
                        branch_points.push_back(b.min);
                        args.push_back(old_args);
                        args.back().push_back(arg);
                    }
                    branch_points.push_back(max);
                } else {
                    size_t k = 0;
                    bool can_subdivide = true;
                    std::vector<int> is_equal(child_branches.size(), false);
                    std::vector<int> intervals(child_branches.size(), -1);
                    for (size_t j = 0; j < child_branches.size(); ++j) {
                        Branch &b = child_branches[j];
                        StmtOrExpr arg;
                        get_branch_content(b, arg);

                        Expr b_limit = simplify(b.min + b.extent);
                        bool in_range;
                        bool found_interval = false;
                        while (!found_interval && can_subdivide && k < args.size()) {
                            Expr min_k = branch_points[k];
                            Expr ext_k = branch_points[k+1] - min_k;
                            if (equal(b.min, min_k)) {
                                // The branch point of the child branch matches exactly a branch
                                // point we have already collected.
                                is_equal[j] = true;
                                intervals[j] = k;
                                found_interval = true;
                            } else if (can_prove_in_range(b.min, min_k, ext_k, in_range)) {
                                if (in_range) {
                                    // We've found an interval that we can prove the child branch
                                    // point lives in.
                                    intervals[j] = k;
                                    found_interval = true;
                                } else {
                                    ++k;
                                }
                            } else {
                                // We can't prove anything about where the child branch point lives, so we are going
                                // to have to recursively divide all the branches. Simple interval subdivision
                                // won't work.
                                can_subdivide = false;
                            }

                        }
                    }

                    if (can_subdivide) {
                        // We can prove where all the child branch points lies w.r.t. to the points already
                        // collected.
                        std::vector<Expr> new_branch_points;
                        std::vector<std::vector<StmtOrExpr> > new_args;
                        for (size_t j = 0; j < child_branches.size(); ++j) {
                            Branch &b = child_branches[j];
                            StmtOrExpr arg;
                            get_branch_content(b, arg);

                            size_t first = intervals[j];
                            size_t last  = j < child_branches.size()-1? intervals[j+1]: args.size();

                            // Case: First interval...
                            Expr first_point = is_equal[first]? branch_points[first]: b.min;
                            new_branch_points.push_back(first_point);
                            new_args.push_back(args[first]);
                            new_args.back().push_back(arg);

                            // Case: Intermediate intervals...
                            for (size_t k = first+1; k < last; ++k) {
                                new_branch_points.push_back(branch_points[k]);
                                new_args.push_back(args[k]);
                                new_args.back().push_back(arg);
                            }

                            // Case: Last interval...
                            if (last > first && j < child_branches.size()-1 && !is_equal[last]) {
                                Expr last_point = branch_points[last];
                                new_branch_points.push_back(last_point);
                                new_args.push_back(args[last]);
                                new_args.back().push_back(arg);
                            }
                        }
                        new_branch_points.push_back(branch_points.back());
                        branch_points.swap(new_branch_points);
                        args.swap(new_args);
                    } else {
                        if (child_branches.size() * args.size() > branching_limit) {
                            // Branching on this child node would introduce too many branches, so we fall back
                            // to adding the child argument directly to the branches collected thus far.
                            for (size_t j = 0; j < args.size(); ++j) {
                                args[j].push_back(child);
                            }
                        } else {
                            // We can't prove where all the child branch points lies w.r.t. to the points already
                            // collected, so we must recursively branch everything we've collected so far.
                            std::vector<Expr> new_branch_points;
                            std::vector<std::vector<StmtOrExpr> > new_args;
                            for (size_t j = 0; j < args.size(); ++j) {
                                Expr min = branch_points[j];
                                Expr max = branch_points[j+1] - 1;
                                for (size_t k = 0; k < child_branches.size(); ++k) {
                                    Branch &b = child_branches[k];
                                    StmtOrExpr arg;
                                    get_branch_content(b, arg);

                                    new_branch_points.push_back(simplify(clamp(b.min, min, max)));
                                    new_args.push_back(args[k]);
                                    new_args.back().push_back(arg);
                                }
                            }
                            new_branch_points.push_back(branch_points.back());
                            branch_points.swap(new_branch_points);
                            args.swap(new_args);
                        }
                    }
                }

                // debug(0) << "  New branch points {";
                // for (size_t j = 0; j < branch_points.size(); ++j) {
                //   debug(0) << branch_points[j] << (j < branch_points.size()-1? ", ": "}\n");
                // }
            }
        }

        // Only add branches in the non-trivial case of more than one branch.
        if (args.size() > 1) {
            // Now we have branched all the child stmt or expr's and we need to rebuild the
            // branches using the collected branch_points and args.
            for (size_t i = 0; i < args.size(); ++i) {
                Expr min = branch_points[i];
                Expr extent = simplify(branch_points[i+1] - min);
                if (!is_zero(extent)) {
                    Branch b = {min, extent, Expr(), Stmt()};
                    update_branch(b, op, args[i]);
                    branches.push_back(b);
                }
            }
        }
    }

    void visit_simple_cond(Expr cond, Expr a, Expr b) {
        // Bail out if this condition depends on more than just the current loop variable.
        if (num_free_vars(cond, scope, free_vars) > 1) return;

        Expr solve = solve_for_linear_variable(cond, name, free_vars, scope);
        if (!solve.same_as(cond)) {
            Branch b1, b2;
            if (build_branches(solve, a, b, b1, b2)) {
                size_t num_branches = branches.size();
                Expr orig_min = min;
                Expr orig_extent = extent;

                if (!is_zero(b1.extent)) {
                    if (branches.size() < branching_limit) {
                        min = b1.min;
                        extent = b1.extent;
                        b1.expr.accept(this);
                    }

                    // If we didn't branch any further, push these branches onto the stack.
                    if (branches.size() == num_branches && !is_zero(b1.extent)) {
                        branches.push_back(b1);
                    }
                    num_branches = branches.size();
                }

                if (!is_zero(b2.extent)) {
                    if (branches.size() < branching_limit) {
                        min = b2.min;
                        extent = b2.extent;
                        b2.expr.accept(this);
                    }

                    // If we didn't branch any further, push these branches onto the stack.
                    if (branches.size() == num_branches && !is_zero(b2.extent)) {
                        branches.push_back(b2);
                    }

                    min = orig_min;
                    extent = orig_extent;
                }
            }
        }
    }

    void update_branch(Branch &b, const Cast *op, const std::vector<StmtOrExpr> &value) {
        b.expr = Cast::make(op->type, value[0]);
    }

    void visit(const Cast *op) {
        std::vector<StmtOrExpr> children(1, op->value);
        branch_children(op, children);
    }

    // void visit(const Variable *op) {
    //     if (scope.contains(op->name)) {
    //         Expr expr = scope.get(op->name);
    //         expr.accept(this);
    //     }
    // }

    template<class Op>
    void update_binary_op_branch(Branch &b, const Op *op, const std::vector<StmtOrExpr> &ab) {
        b.expr = Op::make(ab[0], ab[1]);
    }

    void update_branch(Branch &b, const Add *op, const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const Sub *op, const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const Mul *op, const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const Div *op, const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const Mod *op, const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const Min *op, const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const Max *op, const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}

    void update_branch(Branch &b, const EQ *op,  const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const NE *op,  const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const LT *op,  const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const LE *op,  const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const GT *op,  const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const GE *op,  const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const And *op, const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}
    void update_branch(Branch &b, const Or *op,  const std::vector<StmtOrExpr> &ab) {update_binary_op_branch(b, op, ab);}

    template<class Op>
    void visit_binary_op(const Op *op) {
        std::vector<StmtOrExpr> ab(2, Expr());
        ab[0] = op->a;
        ab[1] = op->b;

        branch_children(op, ab);
    }

    template<class Op, class Cmp>
    void visit_min_or_max(const Op *op) {
        Expr a = op->a;
        Expr b = op->b;

        if (expr_uses_var(a, name, scope) || expr_uses_var(b, name, scope)) {
            Expr cond = Cmp::make(a, b);
            visit_simple_cond(cond, a, b);
        } else {
            visit_binary_op(op);
        }
    }

    void visit(const Add *op) {visit_binary_op(op);}
    void visit(const Sub *op) {visit_binary_op(op);}
    void visit(const Mul *op) {visit_binary_op(op);}
    void visit(const Div *op) {visit_binary_op(op);}
    void visit(const Mod *op) {visit_binary_op(op);}

    void visit(const Min *op) {visit_min_or_max<Min, LE>(op);}
    void visit(const Max *op) {visit_min_or_max<Max, GE>(op);}

    void visit(const EQ *op)  {visit_binary_op(op);}
    void visit(const NE *op)  {visit_binary_op(op);}
    void visit(const LT *op)  {visit_binary_op(op);}
    void visit(const LE *op)  {visit_binary_op(op);}
    void visit(const GT *op)  {visit_binary_op(op);}
    void visit(const GE *op)  {visit_binary_op(op);}
    void visit(const And *op) {visit_binary_op(op);}
    void visit(const Or *op)  {visit_binary_op(op);}

    void update_branch(Branch &b, const Not *op, const std::vector<StmtOrExpr> &ab) {
        b.expr = Not::make(ab[0]);
    }

    void visit(const Not *op) {
        std::vector<StmtOrExpr> a(1, op->a);
        branch_children(op, a);
    }

    void update_branch(Branch &b, const Select *op, const std::vector<StmtOrExpr> &vals) {
        b.expr = Select::make(op->condition, vals[0], vals[1]);
    }

    void visit(const Select *op) {
        if (expr_uses_var(op->condition, name, scope) &&
            op->condition.type().is_scalar()) {
            Expr select = normalize_select(op, scope);
            // debug(0) << "Branching on normalized select: " << select << "\n";
            op = select.as<Select>();
            visit_simple_cond(op->condition, op->true_value, op->false_value);
        } else {
            std::vector<StmtOrExpr> vals(2);
            vals[0] = op->true_value;
            vals[1] = op->false_value;
            branch_children(op, vals);
        }
    }

    void update_branch(Branch &b, const Load *op, const std::vector<StmtOrExpr> &index) {
        b.expr = Load::make(op->type, op->name, index[0], op->image, op->param);
    }

    void visit(const Load *op) {
        std::vector<StmtOrExpr> index(1, op->index);
        branch_children(op, index);
    }

    void update_branch(Branch &b, const Ramp *op, const std::vector<StmtOrExpr> &args) {
        b.expr = Ramp::make(args[0], args[1], op->width);
    }

    void visit(const Ramp *op) {
        std::vector<StmtOrExpr> children(2, Expr());
        children[0] = op->base;
        children[1] = op->stride;
        branch_children(op, children);
    }

    void update_branch(Branch &b, const Broadcast *op, const std::vector<StmtOrExpr> &value) {
        b.expr = Broadcast::make(value[0], op->width);
    }

    void visit(const Broadcast *op) {
        std::vector<StmtOrExpr> value(1, op->value);
        branch_children(op, value);
    }

    void update_branch(Branch &b, const Call *op, const std::vector<StmtOrExpr> &branch_args) {
        std::vector<Expr> args;
        for (size_t i = 0; i < branch_args.size(); ++i) {
            args.push_back(branch_args[i]);
        }
        b.expr = Call::make(op->type, op->name, args, op->call_type,
                            op->func, op->value_index, op->image, op->param);
    }

    void visit(const Call *op) {
        if (op->args.size() > 0) {
            std::vector<StmtOrExpr> args;
            for (size_t i = 0; i < op->args.size(); ++i) {
                args.push_back(op->args[i]);
            }
            branch_children(op, args);
        }
    }

#if 1
    void update_branch(Branch &b, const Let *op, std::vector<StmtOrExpr> &args) {
        b.expr = Let::make(op->name, args[0], args[1]);
    }

    void update_branch(Branch &b, const LetStmt *op, std::vector<StmtOrExpr> &args) {
        b.stmt = LetStmt::make(op->name, args[0], args[1]);
    }

    template<class LetOp>
    void visit_let(const LetOp *op) {
        std::vector<StmtOrExpr> children(2);
        children[0] = op->value;
        children[1] = op->body;
        scope.push(op->name, op->value);
        branch_children(op, children);
        scope.pop(op->name);
    }

    void visit(const Let *op) {visit_let(op);}
    void visit(const LetStmt *op) {visit_let(op);}
#else
    void update_branch(Branch &b, const Let *op, std::vector<StmtOrExpr> &body) {
        b.expr = Let::make(op->name, scope.get(op->name), body[0]);
    }

    void update_branch(Branch &b, const LetStmt *op, std::vector<StmtOrExpr> &body) {
        b.stmt = LetStmt::make(op->name, scope.get(op->name), body[0]);
        // debug(0) << "Updated let branch:\n" << b.stmt << "\n";
    }

    template<class StmtOrExpr, class LetOp>
    void visit_let(const LetOp *op) {
        // First we branch the value of the let.
        size_t old_num_branches = branches.size();
        if (branches.size() < branching_limit) {
            op->value.accept(this);
        }

        if (branches.size() == old_num_branches) {
            // If the value didn't branch we continue to branch the let body normally.
            scope.push(op->name, op->value);
            std::vector<StmtOrExpr> body(1, op->body);
            branch_children(op, body);
            scope.pop(op->name);
        } else {
            // Collect the branches for the let value
            std::vector<Branch> let_branches(branches.begin() + old_num_branches, branches.end());
            branches.erase(branches.begin() + old_num_branches, branches.end());

            // debug(0) << "Branched let " << op->name << " = " << op->value << " into "
            //          << let_branches.size() << " branches.\n";

            Expr old_min = min;
            Expr old_extent = extent;

            std::vector<Branch> new_branches;
            for (size_t i = 0; i < let_branches.size(); ++i) {
                min = let_branches[i].min;
                extent = let_branches[i].extent;

                // debug(0) << "\tBranching body on interval [" << min << ", " << extent << ") with "
                //          << op->name << " = " << let_branches[i].expr << "\n";

                // Now we branch the body, first pushing the value
                // expr from the current value branch into the scope.
                old_num_branches = branches.size();
                scope.push(op->name, let_branches[i].expr);
                std::vector<StmtOrExpr> body(1, op->body);
                branch_children(op, body);
                scope.pop(op->name);

                if (branches.size() == old_num_branches) {
                    // If the body didn't branch then we need to rebuild the let using the current value branch.
                    Branch b = make_branch(min, extent, LetOp::make(op->name, let_branches[i].expr, op->body));
                    branches.push_back(b);
                }
                new_branches.insert(new_branches.end(), branches.begin() + old_num_branches, branches.end());
                branches.erase(branches.begin() + old_num_branches, branches.end());

                debug(0) << "Branching LetStmt: let " << op->name << " = " << let_branches[i].expr << ". "
                         << "Num branches = " << branches.size() << ".\n";
            }

            if (branches.size() + new_branches.size() <= branching_limit) {
                branches.insert(branches.end(), new_branches.begin(), new_branches.end);
            } else {
                for (size_t i = 0; i < let_branches.size(); ++i) {

                }
            }

            min = old_min;
            extent = old_extent;
        }
    }

    void visit(const Let *op) {visit_let<Expr>(op);}
    void visit(const LetStmt *op) {visit_let<Stmt>(op);}
#endif

    // AssertStmt

    void update_branch(Branch &b, const Pipeline *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = Pipeline::make(op->name, args[0], args[1], args[2]);
    }

    void visit(const Pipeline *op) {
        std::vector<StmtOrExpr> children(3);
        children[0] = op->produce;
        children[1] = op->update;
        children[2] = op->consume;
        branch_children(op, children);
    }

#if 1
    void update_branch(Branch &b, const For *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = For::make(op->name, args[0], args[1], op->for_type, args[2]);
    }

    void visit(const For *op) {
        std::vector<StmtOrExpr> children(3);
        children[0] = op->min;
        children[1] = op->extent;
        children[2] = op->body;
        free_vars.push(op->name, 0);
        branch_children(op, children);
        free_vars.pop(op->name);
    }
#else
    void update_branch(Branch &b, const For *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = For::make(op->name, args[0], args[1], op->for_type, op->body);
    }

    void update_branch(Branch &b, const For *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = For::make(op->name, op->min, op->extent, op->for_type, args[0]);
    }

    void visit(const For *op) {
       const Variable *loop_ext = op->extent.as<Variable>();
        if (loop_ext) {
            bounds_info.push(loop_ext->name, Interval(0, loop_ext->type.max()));
        }

        // First we branch the bounds of the for loop.
        size_t old_num_branches = branches.size();
        if (branches.size() < branching_limit) {
            std::vector<Expr> bounds(2, Expr());
            bounds[0] = op->min;
            bounds[1] = op->extent;
            branch_children(op, bounds);
        }

        if (branches.size() == old_num_branches) {
            // The bounds exprs didn't branch so we branch the body of the for loop as usual.
            free_vars.push(op->name, 0);
            std::vector<Stmt> body(1, op->body);
            branch_children(op, body);
            free_vars.pop(op->name);
        } else {
            // Collect all the branches from the bounds exprs
            std::vector<Branch> bounds_branches(branches.begin() + old_num_branches, branches.end());
            branches.erase(branches.begin() + old_num_branches, branches.end());

            // debug(0) << "Branched for(" << op->name << ", " << op->min << ", " << op->extent
            //          << ") into  " << bounds_branches.size() << " branches.\n";

            Expr old_min = min;
            Expr old_extent = extent;

            for (size_t i = 0; i < bounds_branches.size(); ++i) {
                const For *loop = bounds_branches[i].stmt.as<For>();

                min = bounds_branches[i].min;
                extent = bounds_branches[i].extent;

                // debug(0) << "\tBranching for(" << op->name << ") body on interval [" << min << ", " << extent << ")\n";

                old_num_branches = branches.size();
                free_vars.push(op->name, 0);
                std::vector<Stmt> body(1, op->body);
                branch_children(loop, body);
                free_vars.pop(op->name);

                if (branches.size() == old_num_branches) {
                    // If we branched the bound, but did not branch the body then we add the bounds
                    // branch to the branches list here.
                    branches.push_back(bounds_branches[i]);
                }
            }

            min = old_min;
            extent = old_extent;
        }
    }
#endif

    void update_branch(Branch &b, const Store *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = Store::make(op->name, args[0], args[1]);
    }

    void visit(const Store *op) {
        std::vector<StmtOrExpr> args(2);
        args[0] = op->value;
        args[1] = op->index;
        branch_children(op, args);
    }

    // Provide

    void update_branch(Branch &b, const Allocate *op, const std::vector<StmtOrExpr> &body) {
        b.stmt = Allocate::make(op->name, op->type, op->extents, op->condition, body[0]);
    }

    void visit(const Allocate *op) {
        std::vector<StmtOrExpr> children(1, op->body);
        branch_children(op, children);
    }

    void update_branch(Branch &b, const Block *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = Block::make(args[0], args[1]);
    }

    void visit(const Block *op) {
        std::vector<StmtOrExpr> children(2, Stmt());
        children[0] = op->first;
        children[1] = op->rest;
        branch_children(op, children);
    }

    void update_branch(Branch &b, const IfThenElse *op, const std::vector<StmtOrExpr> &cases) {
        b.stmt = IfThenElse::make(op->condition, cases[0], cases[1]);
    }

    void visit(const IfThenElse *op) {
        if (expr_uses_var(op->condition, name, scope)) {
            Stmt normalized = normalize_if_stmts(op, scope);
            normalized = prune_if_stmt_tree(normalized, name, scope, bounds_info, free_vars);
            const IfThenElse *if_stmt = normalized.as<IfThenElse>();

            // debug(0) << "Branching normalized if:\n" << Stmt(if_stmt) << "\n";

            // Bail out if this condition depends on more than just
            // the current loop variable.
            if (num_free_vars(if_stmt->condition, scope, free_vars) > 1) return;

            Expr solve = solve_for_linear_variable(if_stmt->condition, name, free_vars, scope);
            if (!solve.same_as(if_stmt->condition)) {
                Stmt then_stmt = if_stmt->then_case.defined()? if_stmt->then_case: Evaluate::make(0);
                Stmt else_stmt = if_stmt->else_case.defined()? if_stmt->else_case: Evaluate::make(0);
                Branch b1, b2;
                if (build_branches(solve, then_stmt, else_stmt, b1, b2)) {
                    size_t num_branches = branches.size();
                    Expr orig_min = min;
                    Expr orig_extent = extent;

                    if (!is_zero(b1.extent)) {
                        if (branches.size() < branching_limit) {
                            min = b1.min;
                            extent = b1.extent;
                            b1.stmt.accept(this);
                        }

                        // If we didn't branch any further, push this branch onto the stack.
                        if (branches.size() == num_branches && !is_zero(b1.extent)) {
                            branches.push_back(b1);
                        }
                        num_branches = branches.size();
                    }

                    if (!is_zero(b2.extent)) {
                        if (branches.size() < branching_limit) {
                            min = b2.min;
                            extent = b2.extent;
                            b2.stmt.accept(this);
                        }

                        // If we didn't branch any further, push this
                        // branches onto the stack.
                        if (branches.size() == num_branches && !is_zero(b2.extent)) {
                            branches.push_back(b2);
                        }
                    }

                    min = orig_min;
                    extent = orig_extent;

                    return;
                }
            }
        }

        std::vector<StmtOrExpr> cases(2);
        cases[0] = op->then_case;
        cases[1] = op->else_case;
        branch_children(op, cases);
    }

    void update_branch(Branch &b, const Evaluate *op, const std::vector<StmtOrExpr> &value) {
        b.stmt = Evaluate::make(value[0]);
    }

    void visit(const Evaluate *op) {
        std::vector<StmtOrExpr> children(1, op->value);
        branch_children(op, children);
    }
};


class SpecializeBranchedLoops : public IRMutator {
private:
    using IRVisitor::visit;

    Scope<Expr> scope;
    Scope<int> loop_vars;
    Scope<Interval> bounds_info;

    void visit(const For *op) {
        loop_vars.push(op->name, 0);

        const Variable *loop_ext = op->extent.as<Variable>();
        if (loop_ext) {
            bounds_info.push(loop_ext->name, Interval(0, Expr()));
        }

        Stmt body = mutate(op->body);

        bool branched = false;
        if (op->for_type == For::Serial && stmt_branches_in_var(op->name, body, scope)) {
            // debug(0) << "Collecting branches in loop " << op->name << ":\n" << body << "\n\n";
            BranchCollector collector(op->name, op->min, op->extent, &scope, &loop_vars, &bounds_info);
            body.accept(&collector);

            if (!collector.branches.empty()) {
                stmt = collector.construct_stmt();
                branched = true;
            }
        }

        if (!branched) {
            if (!body.same_as(op->body)) {
                stmt = For::make(op->name, op->min, op->extent, op->for_type, body);
            } else {
                stmt = op;
            }
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
#if 0
    debug(0) << "Specializing branched loops in stmt:\n" << s << "\n";
    s = SpecializeBranchedLoops().mutate(s);
    debug(0) << "Specialized stmt:\n" << s << "\n";
    return s;
#else
    return SpecializeBranchedLoops().mutate(s);
#endif
}

namespace {

class CountLoops : public IRVisitor {
    using IRVisitor::visit;

    void visit(const For *op) {
        if (op->name == var) {
            num_loops++;
        }
        op->body.accept(this);
    }

  public:
    std::string var;
    int num_loops;

    CountLoops(const std::string &v) : var(v), num_loops(0) {}
};

int count_loops(Stmt stmt, const std::string &var) {
    CountLoops counter(var);
    stmt.accept(&counter);
    return counter.num_loops;
}

void check_num_branches(Stmt stmt, const std::string &var, int expected_loops) {
    int num_loops = count_loops(stmt, var);

    if (num_loops != expected_loops) {
        internal_error
                << "Expected stmt to branch into " << expected_loops
                << " loops, only found " << num_loops << " loops:\n"
                << stmt << "\n";
    }
}

class CheckIntervals : public IRVisitor {
    using IRVisitor::visit;

    Scope<Expr> scope;
    std::vector<std::string> bound_vars;

    Expr wrap_in_scope(Expr expr) const {
        for (int i = (int)bound_vars.size() - 1; i >= 0; --i) {
            const std::string &var = bound_vars[i];
            expr = Let::make(var, scope.get(var), expr);
        }

        return simplify(expr);
    }

    void visit(const For *op) {
        if (op->name == var) {
            const Interval &iv = ival[index++];
            matches = matches
                    && equal(wrap_in_scope(op->min), iv.min)
                    && equal(wrap_in_scope(op->extent), iv.max);
        }

        op->body.accept(this);
    }

    void visit(const LetStmt *op) {
        scope.push(op->name, op->value);
        bound_vars.push_back(op->name);
        op->body.accept(this);
        bound_vars.pop_back();
        scope.pop(op->name);
    }

  public:
    std::string var;
    const Interval* ival;
    int  index;
    bool matches;

    CheckIntervals(const std::string& v, const Interval* i ) :
            var(v), ival(i), index(0), matches(true)
    {}
};

void check_branch_intervals(Stmt stmt, const std::string& loop_var,
                            const Interval* intervals) {
    CheckIntervals check(loop_var, intervals);
    stmt.accept(&check);
    if (!check.matches) {
        internal_error << "loop branches in unexpected ways:\n" << stmt << "\n";
    }
}

}

void specialize_branched_loops_test() {

    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    {
        // Basic case of branching into 3 loops
        Expr cond = 1 <= x && x < 9;
        Stmt branch = IfThenElse::make(cond, Store::make("out", 1, x),
                                       Store::make("out", 0, x));
        Stmt stmt = For::make("x", 0, 10, For::Serial, branch);
        Stmt branched = specialize_branched_loops(stmt);
        Interval ivals[] = {Interval(0,1), Interval(1,8), Interval(9,1)};
        check_num_branches(branched, "x", 3);
        check_branch_intervals(branched, "x", ivals);
    }

    {
        // Case using an equality.
        Expr cond = x == 5;
        Stmt branch = IfThenElse::make(cond, Store::make("out", 1, x),
                                       Store::make("out", 0, x));
        Stmt stmt = For::make("x", 0, 10, For::Serial, branch);
        Stmt branched = specialize_branched_loops(stmt);
        Interval ivals[] = {Interval(0,5), Interval(5,1), Interval(6,4)};
        check_num_branches(branched, "x", 3);
        check_branch_intervals(branched, "x", ivals);
    }

    {
        // Basic 2D case, branching into 9 loops
        Expr tmp = Variable::make(Int(32), "tmp");
        Expr cond = 1 <= x && x < 9;
        Stmt branch = IfThenElse::make(cond, Store::make("out", tmp + 1, x),
                                       Store::make("out", tmp, x));
        Stmt stmt = For::make("x", 0, 10, For::Serial, branch);

        cond = 1 <= y && y < 9;
        branch = IfThenElse::make(cond, LetStmt::make("tmp", 1, stmt),
                                  LetStmt::make("tmp", 0, stmt));
        stmt = For::make("y", 0, 10, For::Serial, branch);
        Stmt branched = specialize_branched_loops(stmt);
        check_num_branches(branched, "x", 9);
    }

    {
        // More complex case involving multiple logical operators, branching into 5 loops
        Expr cond = (1 <= x && x < 4) || (7 <= x && x < 10);
        Stmt branch = IfThenElse::make(cond, Store::make("out", 1, x),
                                       Store::make("out", 0, x));
        Stmt stmt = For::make("x", 0, 11, For::Serial, branch);
        Stmt branched = specialize_branched_loops(stmt);
        Interval ivals[] = {Interval(0,1), Interval(1,3), Interval(4,3),
                            Interval(7,3), Interval(10,1)};
        check_num_branches(branched, "x", 5);
        check_branch_intervals(branched, "x", ivals);
    }

    {
        // Test that we don't modify loop when we encounter a branch
        // with a more complex condition that we can't solve.
        Expr cond = !Cast::make(UInt(1), Select::make(x == 0 || x > 5, 0, 1));
        Stmt branch = IfThenElse::make(cond, Store::make("out", 1, x),
                                       Store::make("out", 0, x));
        Stmt stmt = For::make("x", 0, 10, For::Serial, branch);
        Stmt branched = specialize_branched_loops(stmt);
        check_num_branches(branched, "x", 1);
    }

    {
        // Test that we can deal with a block of branch stmts.
        Expr load = Load::make(Int(32), "out", x, Buffer(), Parameter());
        Stmt b1 = IfThenElse::make(x < 3, Store::make("out", 0, x),
                                   Store::make("out", 1, x));
        Stmt b2 = IfThenElse::make(x < 5, Store::make("out", 10*load, x),
                                   Store::make("out", 10*load + 1, x));
        Stmt b3 = IfThenElse::make(x < 7, Store::make("out", 100*load, x),
                                   Store::make("out", 100*load + 1, x));
        Stmt branch = Block::make(b1, Block::make(b2, b3));
        Stmt stmt = For::make("x", 0, 10, For::Serial, branch);
        Stmt branched = specialize_branched_loops(stmt);
        check_num_branches(branched, "x", 4);
    }

    {
        // Test that we properly branch on select nodes.
        Expr cond = 0 < y && y < 10;
        Stmt branch = Store::make("out", Select::make(cond,
                                                      Ramp::make(x, 1, 4),
                                                      Broadcast::make(0, 4)), y);
        Stmt stmt = For::make("x", 0, 10, For::Serial, branch);
        stmt = For::make("y", 0, 11, For::Serial, stmt);
        Stmt branched = specialize_branched_loops(stmt);
        check_num_branches(branched, "y", 3);
        Interval ivals[] = {Interval(0,1), Interval(1,9), Interval(10,1)};
        check_branch_intervals(branched, "y", ivals);
    }

    {
        // Test that we handle conditions embedded in let stmts.
        Expr cond = 0 < y && y < 10;
        Expr cond_var = Variable::make(Bool(), "cond");
        Stmt branch = Store::make("out", Select::make(cond_var,
                                                      Ramp::make(x, 1, 4),
                                                      Broadcast::make(0, 4)), y);
        Stmt stmt = LetStmt::make("cond", cond, branch);
        stmt = For::make("x", 0, 10, For::Serial, stmt);
        stmt = For::make("y", 0, 11, For::Serial, stmt);
        Stmt branched = specialize_branched_loops(stmt);
        check_num_branches(branched, "y", 3);
        Interval ivals[] = {Interval(0,1), Interval(1,9), Interval(10,1)};
        check_branch_intervals(branched, "y", ivals);
    }

    std::cout << "specialize_branched_loops test passed" << std::endl;
}

}
}
