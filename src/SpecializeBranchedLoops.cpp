#include <set>
#include <stack>

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

// A compile time variable that limits that maximum depth of
// recursive branching that we allow. This prevents a
// combinatorial explosion of code generation.
static const int maximum_branching_depth = 3;


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
    NormalizeIfStmts(const Scope<Expr> *s) {
        scope.set_containing_scope(s);
    }

  private:
    using IRVisitor::visit;

    Scope<Expr> scope;
    bool in_if_stmt;
    std::stack<Stmt> then_case;
    std::stack<Stmt> else_case;

    void visit(const IfThenElse *op) {
        if (then_case.size() < maximum_branching_depth) {
            in_if_stmt = true;
            then_case.push(op->then_case);
            else_case.push(op->else_case);
            Expr cond = mutate(op->condition);
            in_if_stmt = false;
            stmt = IfThenElse::make(cond, mutate(then_case.top()), mutate(else_case.top()));
            then_case.pop();
            else_case.pop();
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

    template<class LetOp>
    void visit_let(const LetOp *op) {
        scope.push(op->name, op->value);
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const Let *op) {visit_let(op);}
    void visit(const LetStmt *op) {visit_let(op);}
};

Stmt normalize_if_stmts(Stmt stmt, const Scope<Expr> &scope) {
    return NormalizeIfStmts(&scope).mutate(stmt);
}

class NormalizeSelect : public IRMutator {
  public:
    NormalizeSelect(const Scope<Expr> *s) {
        scope.set_containing_scope(s);
    }

  private:
    using IRVisitor::visit;

    Scope<Expr> scope;
    bool in_select_cond;
    std::stack<Expr> true_value;
    std::stack<Expr> false_value;

    void visit(const Select *op) {
        if (true_value.size() < maximum_branching_depth) {
            bool old_in_select_cond = in_select_cond;
            in_select_cond = true;
            true_value.push(op->true_value);
            false_value.push(op->false_value);
            Expr cond = mutate(op->condition);
            in_select_cond = false;
            expr = Select::make(cond, mutate(true_value.top()), mutate(false_value.top()));
            in_select_cond = old_in_select_cond;
            true_value.pop();
            false_value.pop();
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

    template<class LetOp>
    void visit_let(const LetOp *op) {
        scope.push(op->name, op->value);
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const Let *op) {visit_let(op);}
    void visit(const LetStmt *op) {visit_let(op);}
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
            name(n), free_vars(lv), min(m), extent(e), branch_depth(0)
    {
        scope.set_containing_scope(s);
    }

  private:
    using IRVisitor::visit;

    std::vector< std::string > internal_lets;

    int branch_depth;

    Branch make_branch(Expr min, Expr ext, Expr expr) {
        // debug(0) << "\t\tMaking branch [" << min << ", " << ext << ") w/ "
        //          << "expr = " << expr << "\n";

        Branch b = {min, ext, expr, Stmt()};
        return b;
    }

    Branch make_branch(Expr min, Expr ext, Stmt stmt) {
        // debug(0) << "\t\tMaking branch [" << min << ", " << ext << ") w/ "
        //          << "stmt: " << stmt << "\n";

        Branch b = {min, ext, Expr(), stmt};
        return b;
    }

    // Build a pair of branches for 2 exprs, based on a simple inequality conditional.
    // It is assumed that the inequality has been solved and so the variable of interest
    // is on the left hand side.
    template<class StmtOrExpr>
    bool build_branches(Expr cond, StmtOrExpr a, StmtOrExpr b, Branch &b1, Branch &b2) {
        const LE *le = cond.as<LE>();
        const GE *ge = cond.as<GE>();
        const LT *lt = cond.as<LT>();
        const GT *gt = cond.as<GT>();

        // debug(0) << "\tBranching at depth " << branch_depth << " in the range [" << min << ", " << extent << ") "
        //          << "on condition: " << cond << "\n";

        Expr min1 = min, min2;
        Expr ext1, ext2;
        if (le) {
            if (is_zero(le->a)) {
                if (is_zero(le->b) || is_positive_const(le->b)) {
                    ext1 = extent;
                } else {
                    ext1 = 0;
                }
            } else {
                ext1 = simplify(Min::make(Max::make(le->b - min1 + 1, 0), extent));
            }
        } else if (lt) {
            if (is_zero(lt->a)) {
                if (is_positive_const(lt->b)) {
                    ext1 = extent;
                } else {
                    ext1 = 0;
                }
            } else {
                ext1 = simplify(Min::make(Max::make(lt->b - min1, 0), extent));
            }
        } else if (ge) {
            if (is_zero(ge->a)) {
                if (is_zero(ge->b) || is_negative_const(ge->b)) {
                    ext1 = extent;
                } else {
                    ext1 = 0;
                }
            } else {
                ext1 = simplify(Min::make(Max::make(ge->b - min1, 0), extent));
                std::swap(a, b);
            }
        } else if (gt) {
            if (is_zero(gt->a)) {
                if (is_negative_const(gt->b)) {
                    ext1 = extent;
                } else {
                    ext1 = 0;
                }
            } else {
                ext1 = simplify(Min::make(Max::make(gt->b - min1 + 1, 0), extent));
                std::swap(a, b);
            }
        } else {
            return false;
        }

        min2 = simplify(min1 + ext1);
        ext2 = simplify(extent - ext1);

        b1 = make_branch(min1, ext1, a);
        b2 = make_branch(min2, ext2, b);

        return true;
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
            if (build_branches(solve, a, b, b1, b2)) {
                size_t num_branches = branches.size();
                Expr orig_min = min;
                Expr orig_extent = extent;

                branch_depth++;
                if (!is_zero(b1.extent)) {
                    if (branch_depth < maximum_branching_depth) {
                        min = b1.min;
                        extent = b1.extent;
                        b1.expr.accept(this);
                    }

                    // If we didn't branch any further, push these branches onto the stack.
                    if (branches.size() == num_branches) {
                        branches.push_back(b1);
                    }
                    num_branches = branches.size();
                }

                if (!is_zero(b2.extent)) {
                    if (branch_depth < maximum_branching_depth) {
                        min = b2.min;
                        extent = b2.extent;
                        b2.expr.accept(this);
                    }

                    // If we didn't branch any further, push these branches onto the stack.
                    if (branches.size() == num_branches) {
                        branches.push_back(b2);
                    }

                    min = orig_min;
                    extent = orig_extent;
                }
                branch_depth--;
            }
        }
    }

    template<class Op, class Cmp>
    void visit_min_or_max(const Op *op) {
        Expr a = op->a;
        Expr b = op->b;

        if (expr_uses_var(a, name, scope) || expr_uses_var(b, name, scope)) {
            Expr cond = Cmp::make(a, b);
            visit_simple_cond(cond, a, b);
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Min *op) {visit_min_or_max<Min, LE>(op);}
    void visit(const Max *op) {visit_min_or_max<Max, GE>(op);}

    template<class Op>
    void visit_binary_op(const Op *op) {
        size_t old_num_branches = branches.size();
        op->a.accept(this);

        if (branches.size() == old_num_branches) {
            op->b.accept(this);

            if (branches.size() > old_num_branches) {
                for (size_t i = old_num_branches; i < branches.size(); ++i) {
                    Expr b_expr = branches[i].expr;
                    branches[i].expr = Op::make(op->a, b_expr);
                }
            }
        } else {
            std::vector<Branch> a_branches(branches.begin() + old_num_branches, branches.end());
            branches.erase(branches.begin() + old_num_branches, branches.end());

            Expr old_min = min;
            Expr old_extent = extent;

            for (size_t i = 0; i < a_branches.size(); ++i) {
                Branch &a_branch = a_branches[i];

                min = a_branch.min;
                extent = a_branch.extent;
                Expr a_expr = a_branch.expr;

                old_num_branches = branches.size();
                op->b.accept(this);

                if (branches.size() == old_num_branches) {
                    a_branch.expr = Op::make(a_expr, op->b);
                    branches.push_back(a_branch);
                } else {
                    std::vector<Branch> b_branches(branches.begin() + old_num_branches, branches.end());
                    branches.erase(branches.begin() + old_num_branches, branches.end());

                    for (size_t j = 0; j < b_branches.size(); ++j) {
                        Branch &b_branch = b_branches[j];

                        Expr b_expr = b_branch.expr;
                        b_branch.expr = Op::make(a_expr, b_expr);
                        branches.push_back(b_branch);
                    }
                }
            }

            min = old_min;
            extent = old_extent;
        }
    }

    void visit(const Add *op) {visit_binary_op(op);}
    void visit(const Sub *op) {visit_binary_op(op);}
    void visit(const Mul *op) {visit_binary_op(op);}
    void visit(const Div *op) {visit_binary_op(op);}
    void visit(const Mod *op) {visit_binary_op(op);}

    void visit(const EQ *op)  {visit_binary_op(op);}
    void visit(const NE *op)  {visit_binary_op(op);}
    void visit(const LT *op)  {visit_binary_op(op);}
    void visit(const LE *op)  {visit_binary_op(op);}
    void visit(const GT *op)  {visit_binary_op(op);}
    void visit(const GE *op)  {visit_binary_op(op);}
    void visit(const And *op) {visit_binary_op(op);}
    void visit(const Or *op)  {visit_binary_op(op);}

    void visit(const Not *op) {
        size_t old_num_branches = branches.size();
        op->a.accept(this);

        if (branches.size() > old_num_branches) {
            for (size_t i = old_num_branches; i < branches.size(); ++i) {
                branches[i].expr = !branches[i].expr;
            }
        }
    }

    void visit(const Load *op) {
        size_t old_num_branches = branches.size();
        op->index.accept(this);

        if (branches.size() > old_num_branches) {
            for (size_t i = old_num_branches; i < branches.size(); ++i) {
                branches[i].expr = Load::make(op->type, op->name, branches[i].expr, op->image, op->param);
            }
        }
    }

    void visit(const Ramp *op) {
        size_t old_num_branches = branches.size();
        op->base.accept(this);

        if (branches.size() == old_num_branches) {
            op->stride.accept(this);

            if (branches.size() > old_num_branches) {
                for (size_t i = old_num_branches; i < branches.size(); ++i) {
                    Expr stride_expr = branches[i].expr;
                    branches[i].expr = Ramp::make(op->base, stride_expr, op->width);
                }
            }
        } else {
            std::vector<Branch> base_branches(branches.begin() + old_num_branches, branches.end());
            branches.erase(branches.begin() + old_num_branches, branches.end());

            Expr old_min = min;
            Expr old_extent = extent;

            for (size_t i = 0; i < base_branches.size(); ++i) {
                Branch &base_branch = base_branches[i];

                min = base_branch.min;
                extent = base_branch.extent;
                Expr base_expr = base_branch.expr;

                old_num_branches = branches.size();
                op->stride.accept(this);

                if (branches.size() == old_num_branches) {
                    base_branch.expr = Ramp::make(base_expr, op->stride, op->width);
                    branches.push_back(base_branch);
                } else {
                    std::vector<Branch> stride_branches(branches.begin() + old_num_branches, branches.end());
                    branches.erase(branches.begin() + old_num_branches, branches.end());

                    for (size_t j = 0; j < stride_branches.size(); ++j) {
                        Branch &stride_branch = stride_branches[j];

                        Expr stride_expr = stride_branch.expr;
                        stride_branch.expr = Ramp::make(base_expr, stride_expr, op->width);
                        branches.push_back(stride_branch);
                    }
                }
            }

            min = old_min;
            extent = old_extent;
        }
    }

    void visit(const Broadcast *op) {
        size_t old_num_branches = branches.size();
        op->value.accept(this);

        if (branches.size() > old_num_branches) {
            for (size_t i = old_num_branches; i < branches.size(); ++i) {
                branches[i].expr = Broadcast::make(branches[i].expr, op->width);
            }
        }
    }

    void visit(const Call *op) {
        std::vector<size_t> index;
        std::vector<std::vector<Branch> > arg_branches;
        std::vector<Expr> args(op->args.size());
        bool done = false;
        bool has_branches = false;
        while(!done) {
            size_t n = index.size();
            Expr old_min = min;
            Expr old_extent = extent;

            if (n > 0) {
                Branch &curr_branch = arg_branches.back()[index.back()];
                min = curr_branch.min;
                extent = curr_branch.extent;
            }

            size_t old_num_branches = branches.size();
            op->args[n].accept(this);

            if (branches.size() == old_num_branches) {
                Branch b = {min, extent, op->args[n], Stmt()};
                arg_branches.push_back(std::vector<Branch>(1, b));
            } else {
                has_branches = true;
                arg_branches.push_back(std::vector<Branch>(branches.begin() + old_num_branches, branches.end()));
                branches.erase(branches.begin() + old_num_branches, branches.end());
            }

            index.push_back(0);
            if (n == op->args.size()-1) {
                if (has_branches) {
                    while (index.back() < arg_branches.back().size()) {
                        for (size_t i = 0; i < index.size(); ++i) {
                            args[i] = arg_branches[i][index[i]].expr;
                        }

                        Branch &curr_branch = arg_branches.back()[index.back()++];
                        curr_branch.expr = Call::make(op->type, op->name, args, op->call_type,
                                                      op->func, op->value_index, op->image, op->param);
                        branches.push_back(curr_branch);
                    }

                    while (!index.empty() && index.back() >= arg_branches.back().size()) {
                        arg_branches.pop_back();
                        index.pop_back();
                        index.back()++;
                    }

                    if (index.empty()) {
                        done = true;
                    }
                } else {
                    done = true;
                }
            }
        }
    }

    void visit(const Select *op) {
        if (expr_uses_var(op->condition, name, scope) &&
            op->condition.type().is_scalar()) {
            Expr select = normalize_select(op, scope);
            // debug(0) << "Branching on normalized select: " << select << "\n";
            op = select.as<Select>();
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

            branch_depth++;
            for (size_t i = 0; i < expr_branches.size(); ++i) {
                Branch &branch = expr_branches[i];
                branch.stmt = LetStmt::make(op->name, branch.expr, op->body);
                branch.expr = Expr();

                if (branch_depth < maximum_branching_depth) {
                    min = branch.min;
                    extent = branch.extent;
                    branch.stmt.accept(this);
                }

                if (branches.size() == num_branches) {
                    branch.stmt = wrap_lets(branch.stmt);
                    branches.push_back(branch);
                }
                num_branches = branches.size();
            }
            branch_depth--;

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
            Stmt normalized = normalize_if_stmts(op, scope);
            const IfThenElse *if_stmt = normalized.as<IfThenElse>();

            // debug(0) << "Branching normalized if:\n" << Stmt(if_stmt) << "\n";

            // Bail out if this condition depends on more than just
            // the current loop variable.
            if (num_free_vars(if_stmt->condition, free_vars) > 1) return;

            Expr solve = solve_for_linear_variable(if_stmt->condition, name, scope);
            if (!solve.same_as(if_stmt->condition)) {
                Branch b1, b2;
                if (build_branches(solve, if_stmt->then_case, if_stmt->else_case, b1, b2)) {
                    size_t num_branches = branches.size();
                    Expr orig_min = min;
                    Expr orig_extent = extent;

                    branch_depth++;
                    if (!is_zero(b1.extent)) {
                        if (branch_depth < maximum_branching_depth) {
                            min = b1.min;
                            extent = b1.extent;
                            b1.stmt.accept(this);
                        }

                        // If we didn't branch any further, push this branches onto the stack.
                        if (branches.size() == num_branches) {
                            b1.stmt = wrap_lets(b1.stmt);
                            branches.push_back(b1);
                        }
                        num_branches = branches.size();
                    }

                    if (has_else && !is_zero(b2.extent)) {
                        if (branch_depth < maximum_branching_depth) {
                            min = b2.min;
                            extent = b2.extent;
                            b2.stmt.accept(this);
                        }

                        // If we didn't branch any further, push this
                        // branches onto the stack.
                        if (branches.size() == num_branches) {
                            b2.stmt = wrap_lets(b2.stmt);
                            branches.push_back(b2);
                        }
                    }
                    branch_depth--;

                    min = orig_min;
                    extent = orig_extent;

                    return;
                }
            }
        }

        op->then_case.accept(this);
        if (has_else) {
            op->else_case.accept(this);
        }
    }

    void visit(const For *op) {
        size_t b0 = branches.size();
        op->body.accept(this);
        for (size_t i = b0; i < branches.size(); ++i) {
            Branch &b = branches[i];
            b.stmt = For::make(op->name, op->min, op->extent, op->for_type, b.stmt);
        }
    }

    void visit(const Block *op) {
        size_t old_num_branches = branches.size();
        op->first.accept(this);

        if (branches.size() == old_num_branches) {
            op->rest.accept(this);

            if (branches.size() > old_num_branches) {
                for (size_t i = old_num_branches; i < branches.size(); ++i) {
                    Stmt rest_stmt = branches[i].stmt;
                    branches[i].stmt = wrap_lets(Block::make(op->first, rest_stmt));
                }
            }
        } else {
            std::vector<Branch> first_branches(branches.begin() + old_num_branches, branches.end());
            branches.erase(branches.begin() + old_num_branches, branches.end());

            Expr old_min = min;
            Expr old_extent = extent;

            for (size_t i = 0; i < first_branches.size(); ++i) {
                Branch &first_branch = first_branches[i];

                min = first_branch.min;
                extent = first_branch.extent;
                Stmt first_stmt = first_branch.stmt;

                old_num_branches = branches.size();
                op->rest.accept(this);

                if (branches.size() == old_num_branches) {
                    first_branch.stmt = wrap_lets(Block::make(first_stmt, op->rest));
                    branches.push_back(first_branch);
                } else {
                    std::vector<Branch> rest_branches(branches.begin() + old_num_branches, branches.end());
                    branches.erase(branches.begin() + old_num_branches, branches.end());

                    for (size_t j = 0; j < rest_branches.size(); ++j) {
                        Branch &rest_branch = rest_branches[j];

                        Stmt rest_stmt = rest_branch.stmt;
                        rest_branch.stmt = wrap_lets(Block::make(first_stmt, rest_stmt));
                        branches.push_back(rest_branch);
                    }
                }
            }

            min = old_min;
            extent = old_extent;
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
            // debug(0) << "Branching loop " << op->name << ". Original body:\n"
            //          << op->body << "Mutated body:\n" << body;

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
                    if (is_zero(simplify(max(extent, 0)))) continue;
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

    void visit(const For *op) {
        if (op->name == var) {
            const Interval &iv = ival[index++];
            matches = matches && equal(op->min, iv.min)
                    && equal(op->extent, iv.max);
        }

        op->body.accept(this);
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
        check_num_branches(branched, "x", 6);
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
