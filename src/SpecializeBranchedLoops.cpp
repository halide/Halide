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

// A compile time variable that limits that maximum number of branches
// that we generate in this optimization pass. This prevents a
// combinatorial explosion of code generation.
static const int branching_limit = 10;


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
    NormalizeIfStmts(const Scope<Expr> *s) : in_if_stmt(false), branch_count(0) {
        scope.set_containing_scope(s);
    }

private:
    using IRVisitor::visit;

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

std::ostream &operator<<(std::ostream &out, const Branch &b) {
    out << "branch(" << b.min << ", " << b.extent << "): ";
    if (b.expr.defined()) {
        out << b.expr << "\n";
    }
    if (b.stmt.defined()) {
        out << "\n" << b.stmt;
    }
    return out;
}

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

  private:
    using IRVisitor::visit;

    void print_branches() {
        std::cout << "Branch collector has branched loop " << name << " into:\n";
        for (size_t i = 0; i < branches.size(); ++i) {
            std::cout << "\t" << branches[i];
        }
        std::cout << "\n";
    }

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
                ext1 = simplify(Min::make(max(le->b - min1 + 1, 0), extent), true, bounds_info);
            }
        } else if (lt) {
            if (is_zero(lt->a)) {
                if (is_positive_const(lt->b)) {
                    ext1 = extent;
                } else {
                    ext1 = 0;
                }
            } else {
                ext1 = simplify(Min::make(max(lt->b - min1, 0), extent), true, bounds_info);
            }
        } else if (ge) {
            if (is_zero(ge->a)) {
                if (is_zero(ge->b) || is_negative_const(ge->b)) {
                    ext1 = extent;
                } else {
                    ext1 = 0;
                }
            } else {
                ext1 = simplify(Min::make(max(ge->b - min1, 0), extent), true, bounds_info);
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
                ext1 = simplify(Min::make(max(gt->b - min1 + 1, 0), extent), true, bounds_info);
                std::swap(a, b);
            }
        } else {
            return false;
        }

        min2 = simplify(min1 + ext1, true, bounds_info);
        ext2 = simplify(max(extent - ext1, 0), true, bounds_info);

        b1 = make_branch(min1, ext1, a);
        b2 = make_branch(min2, ext2, b);

        return true;
    }

    void copy_branch_content(const Branch &b, Expr &expr) {expr = b.expr;}
    void copy_branch_content(const Branch &b, Stmt &stmt) {stmt = b.stmt;}

    // This function generalizes how we must visit all expr or stmt nodes with child nodes.
    template<class Op, class StmtOrExpr>
    void branch_children(const Op *op, const std::vector<StmtOrExpr>& children) {
        std::vector<size_t> index;
        std::vector<std::vector<Branch> > child_branches;
        std::vector<StmtOrExpr> new_children(children.size());
        bool done = false;
        bool has_branches = false;
        while(!done) {
            size_t n = index.size();
            Expr old_min = min;
            Expr old_extent = extent;

            if (n > 0) {
                Branch &curr_branch = child_branches.back()[index.back()];
                min = curr_branch.min;
                extent = curr_branch.extent;
            }

            size_t old_num_branches = branches.size();
            if (children[n].defined()) {
                children[n].accept(this);
            }

            if (branches.size() == old_num_branches) {
                Branch b = make_branch(min, extent, children[n]);
                child_branches.push_back(std::vector<Branch>(1, b));
            } else {
                has_branches = true;
                child_branches.push_back(std::vector<Branch>(branches.begin() + old_num_branches,
                                                             branches.end()));
                branches.erase(branches.begin() + old_num_branches, branches.end());
            }

            index.push_back(0);
            if (n == children.size()-1) {
                if (has_branches) {
                    while (index.back() < child_branches.back().size()) {
                        for (size_t i = 0; i < index.size(); ++i) {
                            copy_branch_content(child_branches[i][index[i]], new_children[i]);
                        }

                        Branch &curr_branch = child_branches.back()[index.back()++];
                        update_branch(curr_branch, op, new_children);
                        branches.push_back(curr_branch);
                    }

                    while (!index.empty() && index.back() >= child_branches.back().size()) {
                        child_branches.pop_back();
                        index.pop_back();

                        if (!index.empty()) {
                            index.back()++;
                        }
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

    void visit_simple_cond(Expr cond, Expr a, Expr b) {
        // Bail out if this condition depends on more than just the current loop variable.
        if (num_free_vars(cond, scope, free_vars) > 1) return;

        Expr solve = solve_for_linear_variable(cond, name, scope);
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
                    if (branches.size() == num_branches) {
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
                    if (branches.size() == num_branches) {
                        branches.push_back(b2);
                    }

                    min = orig_min;
                    extent = orig_extent;
                }
            }
        }
    }

    void update_branch(Branch &b, const Cast *op, const std::vector<Expr> &value) {
        b.expr = Cast::make(op->type, value[0]);
    }

    void visit(const Cast *op) {
        std::vector<Expr> children(1, op->value);
        branch_children(op, children);
    }

    // void visit(const Variable *op) {
    //     if (scope.contains(op->name)) {
    //         Expr expr = scope.get(op->name);
    //         expr.accept(this);
    //     }
    // }

    template<class Op>
    void update_binary_op_branch(Branch &b, const Op *op, const std::vector<Expr> &ab) {
        b.expr = Op::make(ab[0], ab[1]);
    }

    void update_branch(Branch &b, const Add *op, const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const Sub *op, const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const Mul *op, const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const Div *op, const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const Mod *op, const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const Min *op, const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const Max *op, const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}

    void update_branch(Branch &b, const EQ *op,  const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const NE *op,  const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const LT *op,  const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const LE *op,  const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const GT *op,  const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const GE *op,  const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const And *op, const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}
    void update_branch(Branch &b, const Or *op,  const std::vector<Expr> &ab) {update_binary_op_branch(b, op ,ab);}

    template<class Op>
    void visit_binary_op(const Op *op) {
        std::vector<Expr> ab(2, Expr());
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

    void update_branch(Branch &b, const Not *op, const std::vector<Expr> &ab) {
        b.expr = Not::make(ab[0]);
    }

    void visit(const Not *op) {
        std::vector<Expr> a(1, op->a);
        branch_children(op, a);
    }

    void update_branch(Branch &b, const Select *op, const std::vector<Expr> &vals) {
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
            std::vector<Expr> vals(2);
            vals[0] = op->true_value;
            vals[1] = op->false_value;
            branch_children(op, vals);
        }
    }

    void update_branch(Branch &b, const Load *op, const std::vector<Expr> &index) {
        b.expr = Load::make(op->type, op->name, index[0], op->image, op->param);
    }

    void visit(const Load *op) {
        std::vector<Expr> index(1, op->index);
        branch_children(op, index);
    }

    void update_branch(Branch &b, const Ramp *op, const std::vector<Expr> &args) {
        b.expr = Ramp::make(args[0], args[1], op->width);
    }

    void visit(const Ramp *op) {
        std::vector<Expr> children(2, Expr());
        children[0] = op->base;
        children[1] = op->stride;
        branch_children(op, children);
    }

    void update_branch(Branch &b, const Broadcast *op, const std::vector<Expr> &value) {
        b.expr = Broadcast::make(value[0], op->width);
    }

    void visit(const Broadcast *op) {
        std::vector<Expr> value(1, op->value);
        branch_children(op, value);
    }

    void update_branch(Branch &b, const Call *op, const std::vector<Expr> &args) {
        b.expr = Call::make(op->type, op->name, args, op->call_type,
                            op->func, op->value_index, op->image, op->param);
    }

    void visit(const Call *op) {
        if (op->args.size() > 0) {
            branch_children(op, op->args);
        }
    }

    void update_branch(Branch &b, const Let *op, std::vector<Expr> &body) {
        b.expr = Let::make(op->name, scope.get(op->name), body[0]);
    }

    void update_branch(Branch &b, const LetStmt *op, std::vector<Stmt> &body) {
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

            for (size_t i = 0; i < let_branches.size(); ++i) {
                min = let_branches[i].min;
                extent = let_branches[i].extent;

                // debug(0) << "\tBranching body on interval [" << min << ", " << extent << ") with "
                //          << op->name << " = " << let_branches[i].expr << "\n";

                // Now we branch the body, first pushing the value expr from the current value branch into the scope.
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
            }

            min = old_min;
            extent = old_extent;
        }
    }

    void visit(const Let *op) {visit_let<Expr>(op);}
    void visit(const LetStmt *op) {visit_let<Stmt>(op);}

    // AssertStmt

    void update_branch(Branch &b, const Pipeline *op, const std::vector<Stmt> &args) {
        b.stmt = Pipeline::make(op->name, args[0], args[1], args[2]);
    }

    void visit(const Pipeline *op) {
        std::vector<Stmt> children(3, Stmt());
        children[0] = op->produce;
        children[1] = op->update;
        children[2] = op->consume;
        branch_children(op, children);
    }

    void update_branch(Branch &b, const For *op, const std::vector<Expr> &args) {
        b.stmt = For::make(op->name, args[0], args[1], op->for_type, op->body);
    }

    void update_branch(Branch &b, const For *op, const std::vector<Stmt> &args) {
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

    void update_branch(Branch &b, const Store *op, const std::vector<Expr> &args) {
        b.stmt = Store::make(op->name, args[0], args[1]);
    }

    void visit(const Store *op) {
        std::vector<Expr> args(2);
        args[0] = op->value;
        args[1] = op->index;
        branch_children(op, args);
    }

    // Provide

    void update_branch(Branch &b, const Allocate *op, const std::vector<Stmt> &body) {
        b.stmt = Allocate::make(op->name, op->type, op->extents, op->condition, body[0]);
    }

    void visit(const Allocate *op) {
        std::vector<Stmt> children(1, op->body);
        branch_children(op, children);
    }

    void update_branch(Branch &b, const Block *op, const std::vector<Stmt> &args) {
        b.stmt = Block::make(args[0], args[1]);
    }

    void visit(const Block *op) {
        std::vector<Stmt> children(2, Stmt());
        children[0] = op->first;
        children[1] = op->rest;
        branch_children(op, children);
    }

    void update_branch(Branch &b, const IfThenElse *op, const std::vector<Stmt> &cases) {
        b.stmt = IfThenElse::make(op->condition, cases[0], cases[1]);
    }

    void visit(const IfThenElse *op) {
        bool has_else = op->else_case.defined();
        if (expr_uses_var(op->condition, name, scope)) {
            Stmt normalized = normalize_if_stmts(op, scope);
            const IfThenElse *if_stmt = normalized.as<IfThenElse>();

            // debug(0) << "Branching normalized if:\n" << Stmt(if_stmt) << "\n";

            // Bail out if this condition depends on more than just
            // the current loop variable.
            if (num_free_vars(if_stmt->condition, scope, free_vars) > 1) return;

            Expr solve = solve_for_linear_variable(if_stmt->condition, name, scope);
            if (!solve.same_as(if_stmt->condition)) {
                Branch b1, b2;
                if (build_branches(solve, if_stmt->then_case, if_stmt->else_case, b1, b2)) {
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
                        if (branches.size() == num_branches) {
                            branches.push_back(b1);
                        }
                        num_branches = branches.size();
                    }

                    if (has_else && !is_zero(b2.extent)) {
                        if (branches.size() < branching_limit) {
                            min = b2.min;
                            extent = b2.extent;
                            b2.stmt.accept(this);
                        }

                        // If we didn't branch any further, push this
                        // branches onto the stack.
                        if (branches.size() == num_branches) {
                            branches.push_back(b2);
                        }
                    }

                    min = orig_min;
                    extent = orig_extent;

                    return;
                }
            }
        }

        std::vector<Stmt> cases(2);
        cases[0] = op->then_case;
        cases[1] = op->else_case;
        branch_children(op, cases);
    }

    void update_branch(Branch &b, const Evaluate *op, const std::vector<Expr> &value) {
        b.stmt = Evaluate::make(value[0]);
    }

    void visit(const Evaluate *op) {
        std::vector<Expr> children(1, op->value);
        branch_children(op, children);
    }
};

void collect_branches(Expr expr, const std::string& name, Expr min, Expr extent,
                      std::vector<Branch> &branches, const Scope<Expr> &scope,
                      const Scope<int> &free_vars, const Scope<Interval> &bounds_info) {
    BranchCollector collector(name, min, extent, &scope, &free_vars, &bounds_info);
    expr.accept(&collector);
    branches.swap(collector.branches);
}

void collect_branches(Stmt stmt, const std::string& name, Expr min, Expr extent,
                      std::vector<Branch> &branches, const Scope<Expr> &scope,
                      const Scope<int> &free_vars, const Scope<Interval> &bounds_info) {
    BranchCollector collector(name, min, extent, &scope, &free_vars, &bounds_info);
    stmt.accept(&collector);
    branches.swap(collector.branches);
}

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
            bounds_info.push(loop_ext->name, Interval(0, loop_ext->type.max()));
        }

        Stmt body = mutate(op->body);

        if (op->for_type == For::Serial && stmt_branches_in_var(op->name, body, scope)) {
            // debug(0) << "Branching loop " << op->name << ". Original body:\n"
            //          << op->body << "Mutated body:\n" << body;

            std::vector<Branch> branches;
            collect_branches(body, op->name, op->min, op->extent, branches, scope, loop_vars, bounds_info);

            if (branches.empty()) {
                stmt = For::make(op->name, op->min, op->extent, op->for_type, body);
            } else {
                stmt = Evaluate::make(0);
                for (int i = branches.size()-1; i >= 0; --i) {
                    Branch &branch = branches[i];
                    bounds_info.push(op->name, Interval(branch.min, branch.min + branch.extent - 1));
                    Expr extent = simplify(branch.extent, true, bounds_info);
                    if (is_zero(extent)) continue;
                    Stmt branch_stmt = simplify(branch.stmt, true, bounds_info);
                    branch_stmt = For::make(op->name, branch.min, extent, op->for_type, branch_stmt);
                    stmt = Block::make(branch_stmt, stmt);
                    bounds_info.pop(op->name);
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
