#include <queue>
#include <vector>

#include "SpecializeBranchedLoops.h"
#include "BranchVisitors.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LinearSolve.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

// A compile time variable that limits that maximum number of branches that we can generate
// in this optimization pass. This prevents a combinatorial explosion of code generation.
static const int branching_limit = 10;

struct Branch {
    Expr min;
    Expr extent;
    Expr expr;
    Stmt stmt;
};

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

std::ostream& operator<< (std::ostream& out, StmtOrExpr se) {
    if (se.is_expr()) {
        return out << static_cast<Expr>(se);
    } else {
        return out << static_cast<Stmt>(se);
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

class CountFreeVars : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    const Scope<int> &vars;
    Scope<Expr> scope;

    void visit(const Variable *v) {
        if (vars.contains(v->name)) {
            free_vars.insert(v->name);
        } else if (scope.contains(v->name)) {
            include(scope.get(v->name));
        }
    }
public:
    CountFreeVars(const Scope<int> &v, const Scope<Expr> *s = NULL) : vars(v) {
        scope.set_containing_scope(s);
    }
    std::set<std::string> free_vars;
};

size_t num_free_vars(Expr expr, const Scope<int> &free_vars, const Scope<Expr> &scope) {
    CountFreeVars count(free_vars, &scope);
    expr.accept(&count);
    return count.free_vars.size();
}

bool has_free_vars(Expr expr, const Scope<int> &free_vars, const Scope<Expr> &scope) {
    return expr_uses_vars(expr, free_vars, scope);
}

/* This visitor performs the main work for the specialize_branched_loops optimization pass.
 * We are given a variable that we are checking for branches in, a current scope, a list of
 * any other free variables, and some bounds infor on those variables. This visitor then
 * applies to a Stmt in the branch variable and attempts to build up a list of Branch structs,
 * which define the bounds and contents of each branch of the loop.
 */
class BranchCollector : public IRGraphVisitor {
public:
    BranchCollector(const std::string &n, Expr m, Expr e, const Scope<Expr> *s,
                    const Scope<int> *lv, const Scope<Interval> *bi) :
            name(n), min(m), extent(e)
    {
        scope.set_containing_scope(s);
        free_vars.set_containing_scope(lv);
        bounds_info.set_containing_scope(bi);
    }

    bool has_branches() const {
        return !branches.empty();
    }

    /* This method is used to reconstruct the branched loop stmt after we have collected everything.
     */
    Stmt construct_stmt() {
        Stmt stmt;
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
            if (should_extract(b_extent)) {
                std::string extent_name = branch_name.str() + ".extent";
                bounds_vars.push(extent_name);
                scope.push(extent_name, b_extent);
                b_extent = Variable::make(b_extent.type(), extent_name);
            }

            if (should_extract(b_min)) {
                std::string min_name = branch_name.str() + ".min";
                bounds_vars.push(min_name);
                scope.push(min_name, b_min);
                b_min = Variable::make(b_min.type(), min_name);
            }

            Stmt branch_stmt = simplify(b.stmt, true, bounds_info);
            branch_stmt = For::make(name, b_min, b_extent, For::Serial, branch_stmt);
            if (!stmt.defined()) {
                stmt = branch_stmt;
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
    std::string name;
    std::vector<Branch> branches;
    Scope<Expr> scope;
    Scope<int> free_vars;
    Scope<int> bound_vars_linearity;
    Scope<Interval> bounds_info;
    Expr min;
    Expr extent;

    using IRGraphVisitor::visit;

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

    void include_stmt_or_expr(const StmtOrExpr &se) {
        if (se.is_expr()) {
            include(static_cast<Expr>(se));
        } else if (se.is_stmt()) {
            include(static_cast<Stmt>(se));
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

    Branch make_branch(Expr min, Expr extent, StmtOrExpr content) {
        Branch b;
        b.min = min;
        b.extent = extent;
        if (content.is_expr()) {
            b.expr = simplify(static_cast<Expr>(content), true, bounds_info);
        } else if (content.is_stmt()){
            b.stmt = simplify(static_cast<Stmt>(content), true, bounds_info);
        }

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
    bool build_branches(Expr cond, StmtOrExpr a, StmtOrExpr b, Branch &b1, Branch &b2) {
        Expr max = simplify(min + extent - 1);
        bounds_info.push(name, Interval(min, max));

        debug(3) << "Branching on condition " << cond << "\n";

        Expr min1, min2;
        Expr ext1, ext2;

        bool swap;
        bool in_range;
        Expr point = branch_point(cond, swap);
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
    template<class Op>
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
                include_stmt_or_expr(child);
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
                                ++k;
                            }
                        }

                        if (!found_interval) {
                            // We can't prove anything about where the child branch point lives, so we are going
                            // to have to recursively divide all the branches. Simple interval subdivision
                            // won't work.
                            can_subdivide = false;
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

                        if (new_args.size() > branching_limit) {
                            // Branching on this child node would introduce too many branches, so we fall back
                            // to adding the child argument directly to the branches collected thus far.
                            for (size_t j = 0; j < args.size(); ++j) {
                                args[j].push_back(child);
                            }
                        } else {
                            new_branch_points.push_back(branch_points.back());
                            branch_points.swap(new_branch_points);
                            args.swap(new_args);
                        }
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
                                    new_args.push_back(args[j]);
                                    new_args.back().push_back(arg);
                                }
                            }
                            new_branch_points.push_back(branch_points.back());
                            branch_points.swap(new_branch_points);
                            args.swap(new_args);
                        }
                    }
                }
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
                    bounds_info.push(name, Interval(min, simplify(min + extent - 1)));
                    if (b.expr.defined()) {
                        b.expr = simplify(b.expr, true, bounds_info);
                    } else if (b.stmt.defined()) {
                        b.stmt = simplify(b.stmt, true, bounds_info);
                    }
                    bounds_info.pop(name);
                    branches.push_back(b);
                }
            }
        }
    }

    void visit_simple_cond(Expr cond, Expr a, Expr b) {
        // Bail out if this condition depends on more than just the current loop variable.
        if (num_free_vars(cond, free_vars, scope) > 1) return;

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
                        include(b1.expr);
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
                        include(b2.expr);
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

    void visit(const Cast *op) {branch_children(op, vec<StmtOrExpr>(op->value));}

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
    void visit_binary_op(const Op *op) {branch_children(op, vec<StmtOrExpr>(op->a, op->b));}

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

    void visit(const Not *op) {branch_children(op, vec<StmtOrExpr>(op->a));}

    void update_branch(Branch &b, const Select *op, const std::vector<StmtOrExpr> &vals) {
        b.expr = Select::make(op->condition, vals[0], vals[1]);
    }

    void visit(const Select *op) {
        if (expr_is_linear_in_var(op->condition, name, bound_vars_linearity) &&
            op->condition.type().is_scalar()) {
            Expr select = normalize_branch_conditions(op, name, scope, bounds_info, free_vars, branching_limit);
            op = select.as<Select>();
            visit_simple_cond(op->condition, op->true_value, op->false_value);
        } else {
            branch_children(op, vec<StmtOrExpr>(op->true_value, op->false_value));
        }
    }

    void update_branch(Branch &b, const Load *op, const std::vector<StmtOrExpr> &index) {
        b.expr = Load::make(op->type, op->name, index[0], op->image, op->param);
    }

    void visit(const Load *op) {branch_children(op, vec<StmtOrExpr>(op->index));}

    void update_branch(Branch &b, const Ramp *op, const std::vector<StmtOrExpr> &args) {
        b.expr = Ramp::make(args[0], args[1], op->width);
    }

    void visit(const Ramp *op) {branch_children(op, vec<StmtOrExpr>(op->base, op->stride));}

    void update_branch(Branch &b, const Broadcast *op, const std::vector<StmtOrExpr> &value) {
        b.expr = Broadcast::make(value[0], op->width);
    }

    void visit(const Broadcast *op) {branch_children(op, vec<StmtOrExpr>(op->value));}

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

    void update_branch(Branch &b, const Let *op, const std::vector<StmtOrExpr> &body) {
        b.expr = Let::make(op->name, scope.get(op->name), body[0]);
    }

    void update_branch(Branch &b, const LetStmt *op, const std::vector<StmtOrExpr> &body) {
        b.stmt = LetStmt::make(op->name, scope.get(op->name), body[0]);
    }

    template<class LetOp>
    void visit_let(const LetOp *op) {
        // First we branch the value of the let.
        size_t old_num_branches = branches.size();
        if (branches.size() < branching_limit) {
            include(op->value);
        }

        if (branches.size() == old_num_branches) {
            // If the value didn't branch we continue to branch the let body normally.
            int linearity = expr_linearity(op->value, free_vars, bound_vars_linearity);
            bound_vars_linearity.push(op->name, linearity);
            scope.push(op->name, op->value);
            branch_children(op, vec<StmtOrExpr>(op->body));
            bound_vars_linearity.pop(op->name);
            scope.pop(op->name);
        } else {
            // Collect the branches from the let value
            std::vector<Branch> let_branches(branches.begin() + old_num_branches, branches.end());
            branches.erase(branches.begin() + old_num_branches, branches.end());

            Expr old_min = min;
            Expr old_extent = extent;

            std::vector<Branch> new_branches;
            for (size_t i = 0; i < let_branches.size(); ++i) {
                min = let_branches[i].min;
                extent = let_branches[i].extent;

                // Now we branch the body, first pushing the value expr from the current value branch into the scope.
                old_num_branches = branches.size();
                int linearity = expr_linearity(let_branches[i].expr, free_vars, bound_vars_linearity);
                bound_vars_linearity.push(op->name, linearity);
                scope.push(op->name, let_branches[i].expr);
                branch_children(op, vec<StmtOrExpr>(op->body));
                bound_vars_linearity.pop(op->name);
                scope.pop(op->name);

                if (branches.size() == old_num_branches) {
                    // If the body didn't branch then we need to rebuild the let using the current value branch.
                    Branch b = make_branch(min, extent, LetOp::make(op->name, let_branches[i].expr, op->body));
                    branches.push_back(b);
                }

                new_branches.insert(new_branches.end(), branches.begin() + old_num_branches, branches.end());
                branches.erase(branches.begin() + old_num_branches, branches.end());
            }

            if (branches.size() + new_branches.size() <= branching_limit) {
                branches.insert(branches.end(), new_branches.begin(), new_branches.end());
            } else {
                for (size_t i = 0; i < let_branches.size(); ++i) {
                    Branch b = let_branches[i];
                    scope.push(op->name, b.expr);
                    b.expr = Expr();
                    update_branch(b, op, vec<StmtOrExpr>(op->body));
                    branches.push_back(let_branches[i]);
                    scope.pop(op->name);
                }
            }

            min = old_min;
            extent = old_extent;
        }
    }

    void visit(const Let *op) {visit_let(op);}
    void visit(const LetStmt *op) {visit_let(op);}

    void update_branch(Branch &b, const Pipeline *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = Pipeline::make(op->name, args[0], args[1], args[2]);
    }

    void visit(const Pipeline *op) {
        branch_children(op, vec<StmtOrExpr>(op->produce, op->update, op->consume));
    }

    void update_branch(Branch &b, const For *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = For::make(op->name, args[0], args[1], op->for_type, args[2]);
    }

    void visit(const For *op) {
        free_vars.push(op->name, 0);
        branch_children(op, vec<StmtOrExpr>(op->min, op->extent, op->body));
        free_vars.pop(op->name);
    }

    void update_branch(Branch &b, const Store *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = Store::make(op->name, args[0], args[1]);
    }

    void visit(const Store *op) {branch_children(op, vec<StmtOrExpr>(op->value, op->index));}

    void update_branch(Branch &b, const Allocate *op, const std::vector<StmtOrExpr> &body) {
        b.stmt = Allocate::make(op->name, op->type, op->extents, op->condition, body[0]);
    }

    void visit(const Allocate *op) {branch_children(op, vec<StmtOrExpr>(op->body));}

    void update_branch(Branch &b, const Block *op, const std::vector<StmtOrExpr> &args) {
        b.stmt = Block::make(args[0], args[1]);
    }

    void visit(const Block *op) {branch_children(op, vec<StmtOrExpr>(op->first, op->rest));}

    void update_branch(Branch &b, const IfThenElse *op, const std::vector<StmtOrExpr> &cases) {
        b.stmt = IfThenElse::make(op->condition, cases[0], cases[1]);
    }

    void visit(const IfThenElse *op) {
        if (expr_is_linear_in_var(op->condition, name, bound_vars_linearity)) {
            Stmt normalized = normalize_branch_conditions(op, name, scope, bounds_info, free_vars, branching_limit);
            const IfThenElse *if_stmt = normalized.as<IfThenElse>();

            // Bail out if this condition depends on more than just the current loop variable.
            if (num_free_vars(if_stmt->condition, free_vars, scope) > 1) return;

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
                            include(b1.stmt);
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
                            include(b2.stmt);
                        }

                        // If we didn't branch any further, push this branches onto the stack.
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

        branch_children(op, vec<StmtOrExpr>(op->then_case, op->else_case));
    }

    void update_branch(Branch &b, const Evaluate *op, const std::vector<StmtOrExpr> &value) {
        b.stmt = Evaluate::make(value[0]);
    }

    void visit(const Evaluate *op) {
        branch_children(op, vec<StmtOrExpr>(op->value));
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
        if (op->for_type == For::Serial && branches_linearly_in_var(body, op->name)) {
            BranchCollector collector(op->name, op->min, op->extent, &scope, &loop_vars, &bounds_info);
            body.accept(&collector);

            if (collector.has_branches()) {
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
    return SpecializeBranchedLoops().mutate(s);
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

    // This test is broken due to the select condition being pulled out into a let node,
    // and we currently are not diving into bound vars in the branch visitors.
    /* {
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
    } */

    std::cout << "specialize_branched_loops test passed" << std::endl;
}

}
}
