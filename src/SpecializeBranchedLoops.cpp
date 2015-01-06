#include <queue>
#include <set>
#include <stack>
#include <string>
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

using std::queue;
using std::set;
using std::stack;
using std::string;
using std::vector;

// A compile time variable that limits that maximum number of branches that we can generate
// in this optimization pass. This prevents a combinatorial explosion of code generation.
static const size_t branching_limit = 10;

namespace {
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

Expr branch_point(Expr cond, Expr min, Expr extent, bool &swap) {
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
}

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

    template<class T>
    const T *as() {
        if (is_stmt()) {
            return stmt.as<T>();
        } else {
            return expr.as<T>();
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

StmtOrExpr simplify(StmtOrExpr s_or_e, bool simplify_lets = true,
                    const Scope<Interval> &bounds = Scope<Interval>::empty_scope(),
                    const Scope<ModulusRemainder> &alignment = Scope<ModulusRemainder>::empty_scope()) {
    StmtOrExpr result;
    if (s_or_e.is_expr()) {
        result = simplify(static_cast<Expr>(s_or_e), simplify_lets, bounds, alignment);
    } else if (s_or_e.is_stmt()) {
        result = simplify(static_cast<Stmt>(s_or_e), simplify_lets, bounds, alignment);
    }
    return result;
}

std::ostream& operator<< (std::ostream& out, StmtOrExpr se) {
    if (se.is_expr()) {
        return out << static_cast<Expr>(se);
    } else {
        return out << static_cast<Stmt>(se);
    }
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
    set<string> free_vars;
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
class BranchCollector : public IRVisitor {
public:
    BranchCollector(const string &n, Expr min, Expr extent, const Scope<Expr> *s,
                    const Scope<int> *lv, const Scope<Interval> *bi) :
            name(n), branching_vars(false), branches()
    {
        free_vars.set_containing_scope(lv);
        bounds_info.set_containing_scope(bi);
        scope.set_containing_scope(s);
        push_bounds(min, extent);
    }

    bool has_branches() const {
        return !branches.empty();
    }

    /* This method is used to reconstruct the branched loop stmt after we have collected everything.
     */
    Stmt construct_stmt() {
        Stmt stmt;
        queue<string> bounds_vars;
        for (int i = branches.size()-1; i >= 0; --i) {
            Branch &branch = branches[i];
            if (!branch.content.defined()) continue;

            Expr branch_extent = branch.extent;
            Expr branch_min = branch.min;
            Expr branch_max = simplify(branch_min + branch_extent - 1);

            bounds_info.push(name, Interval(branch_min, branch_max));

            // First, we replace the min/extent exprs in the branch by
            // unique variables, pushing the corresponding exprs onto the
            // branch_scope. Before actually adding the branch to the list
            // of branches.
            string branch_name = name + ".b" + int_to_string(i);
            if (should_extract(branch_extent)) {
                string extent_name = branch_name + ".extent";
                bounds_vars.push(extent_name);
                scope.push(extent_name, branch_extent);
                branch_extent = Variable::make(branch_extent.type(), extent_name);
            }

            if (should_extract(branch_min)) {
                string min_name = branch_name + ".min";
                bounds_vars.push(min_name);
                scope.push(min_name, branch_min);
                branch_min = Variable::make(branch_min.type(), min_name);
            }

            Stmt branch_stmt = branch.content;
            branch_stmt = simplify(branch_stmt, true, bounds_info);
            branch_stmt = For::make(name, branch_min, branch_extent, For::Serial, branch_stmt);
            if (!stmt.defined()) {
                stmt = branch_stmt;
            } else {
                stmt = Block::make(branch_stmt, stmt);
            }

            bounds_info.pop(name);
        }

        while (!bounds_vars.empty()) {
            const string &var = bounds_vars.front();
            stmt = LetStmt::make(var, scope.get(var), stmt);
            scope.pop(var);
            bounds_vars.pop();
        }

        return stmt;
    }

private:
    struct Branch {
        Expr min;
        Expr extent;
        StmtOrExpr content;
    };

    string name;

    Scope<int> free_vars;
    Scope<Interval> bounds_info;
    bool branching_vars;

    // These variables store the actual branches.
    vector<Branch> branches;
    Scope<vector<Branch> > let_branches;
    Scope<int> let_num_branches;

    // These are the scopes that the branch objects point to.
    Scope<Expr> scope;
    Scope<int> linearity;

    // These stacks define the current state of the collector. The top() values should always point to the
    // corresponding values of the current branch that we are working in.
    stack<Expr> curr_min;
    stack<Expr> curr_extent;

    using IRVisitor::visit;

    friend std::ostream &operator<<(std::ostream &out, const Branch &b) {
        out << "branch(" << b.min << ", " << b.extent << "): {";
        if (b.content.is_stmt()) out << "\n";
        out << b.content << "}";
        return out;
    }

    void print_branches() {
        std::cout << "Branch collector has branched loop " << name << " into:\n";
        for (size_t i = 0; i < branches.size(); ++i) {
            std::cout << "\t" << branches[i] << "\n";
        }
        std::cout << "\n";
    }

    void collect(StmtOrExpr se, vector<Branch>& se_branches) {
        if (se.defined()) {
            branches.swap(se_branches);
            se.accept(this);
            branches.swap(se_branches);
        } else {
            branches.swap(se_branches);
            add_branch(se, curr_min.top(), curr_extent.top());
            branches.swap(se_branches);
        }
    }

    void push_bounds(Expr min, Expr extent) {
        Expr max = simplify(min + extent - 1);
        curr_min.push(min);
        curr_extent.push(extent);
        bounds_info.push(name, Interval(min, max));
    }

    void pop_bounds() {
        curr_min.pop();
        curr_extent.pop();
        bounds_info.pop(name);
    }

    Branch &last_branch() {
        return branches.back();
    }

    bool add_branch(StmtOrExpr content) {
        if (branches.size() >= branching_limit) {
            return false;
        } else if (is_zero(curr_extent.top())) {
            return true;
        }

        Branch branch;
        branch.min = curr_min.top();
        branch.extent = curr_extent.top();
        branch.content = content;
        branches.push_back(branch);

        return true;
    }

    bool add_branch(StmtOrExpr content, Expr branch_min, Expr branch_extent) {
        push_bounds(branch_min, branch_extent);
        bool result = add_branch(content);
        pop_bounds();
        return result;
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

    // Build a pair of branches for 2 exprs, based on a simple inequality conditional.
    // It is assumed that the inequality has been solved and so the variable of interest
    // is on the left hand side.
    bool add_branches(Expr cond, StmtOrExpr a, StmtOrExpr b) {
        debug(3) << "Branching on condition: " << cond << "\n";

        Expr min = curr_min.top();
        Expr extent = curr_extent.top();
        Expr max = simplify(min + extent - 1);

        Expr min1, min2;
        Expr ext1, ext2;

        bool swap;
        bool in_range;
        Expr point = branch_point(cond, min, extent, swap);
        if (swap) {
            std::swap(a, b);
        }

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

        if (add_branch(a, min1, ext1)) {
            if (add_branch(b, min2, ext2)) {
                return true;
            } else {
                // The second branch couldn't be added so we need to bailout.
                branches.pop_back();
                return false;
            }
        }

        return false;
    }

    template<class Op>
    void merge_child_branches(const Op *op, const vector<vector<Branch> >& child_branches) {
        vector<Expr> branch_points;
        vector<vector<StmtOrExpr> > args;

        for (size_t i = 0; i < child_branches[0].size(); ++i) {
            Branch branch = child_branches[0][i];
            branch_points.push_back(branch.min);
            args.push_back(vec(branch.content));
        }

        Expr min = curr_min.top();
        Expr extent = curr_extent.top();
        Expr max = simplify(min + extent);
        branch_points.push_back(max);

        for (size_t i = 1; i < child_branches.size(); ++i) {
            if (args.size() == 1) {
                // None of the child nodes have actually introduced branches yet, we can simply insert all the
                // branch points from this child branch into the branch_points array.
                vector<StmtOrExpr> old_args = args[0];

                args[0].push_back(child_branches[i][0].content);
                branch_points.pop_back();
                for (size_t j = 1; j < child_branches[i].size(); ++j) {
                    Branch branch = child_branches[i][j];
                    branch_points.push_back(branch.min);
                    args.push_back(old_args);
                    args.back().push_back(branch.content);
                }
                branch_points.push_back(max);
            } else {
                size_t k = 0;
                bool can_subdivide = true;
                vector<int> is_equal(child_branches[i].size(), false);
                vector<int> intervals(child_branches[i].size(), -1);
                for (size_t j = 0; j < child_branches[i].size(); ++j) {
                    Branch branch = child_branches[i][j];
                    // Expr branch_limit = simplify(branch.min + branch.extent);
                    bool in_range;
                    bool found_interval = false;
                    while (!found_interval && can_subdivide && k < args.size()) {
                        Expr min_k = branch_points[k];
                        Expr ext_k = branch_points[k+1] - min_k;
                        if (equal(branch.min, min_k)) {
                            // The branch point of the child branch matches exactly a branch
                            // point we have already collected.
                            is_equal[j] = true;
                            intervals[j] = k;
                            found_interval = true;
                        } else if (can_prove_in_range(branch.min, min_k, ext_k, in_range)) {
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
                    vector<Expr> new_branch_points;
                    vector<vector<StmtOrExpr> > new_args;
                    new_branch_points.reserve(branching_limit + 2);
                    new_args.reserve(branching_limit + 1);
                    for (size_t j = 0; j < child_branches[i].size(); ++j) {
                        Branch branch = child_branches[i][j];
                        size_t first = intervals[j];
                        size_t last  = j < child_branches[i].size()-1? intervals[j+1]: args.size();

                        // Case: First interval...
                        Expr first_point = is_equal[j]? branch_points[first]: branch.min;
                        new_branch_points.push_back(first_point);
                        new_args.push_back(args[first]);
                        new_args.back().push_back(branch.content);

                        if (new_args.size() > branching_limit) {
                            goto fail;
                        }

                        // Case: Intermediate intervals...
                        for (size_t k = first+1; k < last; ++k) {
                            new_branch_points.push_back(branch_points[k]);
                            new_args.push_back(args[k]);
                            new_args.back().push_back(branch.content);

                            if (new_args.size() > branching_limit) {
                                goto fail;
                            }
                        }

                        // Case: Last interval...
                        if (last > first && j < child_branches[i].size()-1 && !is_equal[j+1]) {
                            Expr last_point = branch_points[last];
                            new_branch_points.push_back(last_point);
                            new_args.push_back(args[last]);
                            new_args.back().push_back(branch.content);

                            if (new_args.size() > branching_limit) {
                                goto fail;
                            }
                        }
                    }

                    new_branch_points.push_back(branch_points.back());
                    branch_points.swap(new_branch_points);
                    args.swap(new_args);
                } else {
                    if (child_branches[i].size() * args.size() > branching_limit) {
                        goto fail;
                    } else {
                        // We can't prove where all the child branch points lies w.r.t. to the points already
                        // collected, so we must recursively branch everything we've collected so far.
                        vector<Expr> new_branch_points;
                        vector<vector<StmtOrExpr> > new_args;
                        for (size_t j = 0; j < args.size(); ++j) {
                            Expr min = branch_points[j];
                            Expr max = simplify(branch_points[j+1] - 1);
                            for (size_t k = 0; k < child_branches[i].size(); ++k) {
                                Branch branch = child_branches[i][k];
                                new_branch_points.push_back(simplify(clamp(branch.min, min, max)));
                                new_args.push_back(args[j]);
                                new_args.back().push_back(branch.content);
                            }
                        }
                        new_branch_points.push_back(branch_points.back());
                        branch_points.swap(new_branch_points);
                        args.swap(new_args);
                    }
                }
            }
        }

        // Now we have branched all the child branch points and contents and we need to build the branches.
        for (size_t i = 0; i < args.size(); ++i) {
            Expr branch_min = branch_points[i];
            Expr branch_max = simplify(branch_points[i+1] - 1);
            Expr branch_extent = simplify(branch_points[i+1] - branch_min);
            if (!is_zero(branch_extent)) {
                push_bounds(branch_min, branch_extent);
                // This should probably never happen as we have detected overflow above.
                if (!add_branch(make_branch_content(op, args[i]))) {
                    for (size_t j = 0; j < i; ++j) {
                        branches.pop_back();
                    }
                    pop_bounds();
                    goto fail;
                }

                Branch &branch = last_branch();
                branch.content = simplify(branch.content, true, bounds_info);
                pop_bounds();
            }
        }

        return;

      fail:
        add_branch(StmtOrExpr(op));
    }

    bool visit_simple_cond(Expr cond, StmtOrExpr a, StmtOrExpr b) {
        size_t num_defined = 0;
        if (a.defined()) ++num_defined;
        if (b.defined()) ++num_defined;

        // Bail out if this condition depends on more than just the current loop variable,
        // or we are going to branch too much.
        if (num_free_vars(cond, free_vars, scope) > 1 ||
            branches.size() + num_defined > branching_limit) {
            return false;
        }

        Expr solve = solve_for_linear_variable(cond, name, free_vars, scope);
        if (!solve.same_as(cond)) {
            int num_old_branches = branches.size();
            if (add_branches(solve, a, b)) {
                int num_children = branches.size() - num_old_branches;
                for (int i = 0; i < num_children; ++i) {
                    vector<Branch>::iterator child_branch = branches.begin() + num_old_branches;
                    Branch branch = *child_branch;
                    branches.erase(child_branch);

                    push_bounds(branch.min, branch.extent);
                    collect(branch.content, branches);
                    pop_bounds();
                }
                return true;
            }
        }

        return false;
    }

    void visit(const IntImm *op)    {add_branch(Expr(op));}
    void visit(const FloatImm *op)  {add_branch(Expr(op));}
    void visit(const StringImm *op) {add_branch(Expr(op));}

    void visit(const Cast *op) {
        vector<Branch> value_branches;
        collect(op->value, value_branches);
        for (size_t i = 0; i < value_branches.size(); ++i) {
            Branch &b = value_branches[i];
            Expr value = b.content;
            if (value.same_as(op->value)) {
                b.content = Expr(op);
            } else {
                b.content = Cast::make(op->type, value);
            }
            branches.push_back(b);
        }
    }

    void visit(const Variable *op) {
        if (let_branches.contains(op->name)) {
            vector<Branch> &var_branches = let_branches.ref(op->name);

            for (size_t i = 0; i < var_branches.size(); ++i) {
                Branch &branch = var_branches[i];
                string new_name = op->name;
                if (var_branches.size() > 1) {
                    new_name += "." + int_to_string(i);
                }
                branch.content = Variable::make(op->type, new_name);
                branches.push_back(branch);
            }
        } else {
            add_branch(Expr(op));
        }
    }

    template<class Op>
    StmtOrExpr binary_op_branch_content(const Op *op, const vector<StmtOrExpr> &ab) {
        return Op::make(ab[0], ab[1]);
    }

    StmtOrExpr make_branch_content(const Add *op, const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const Sub *op, const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const Mul *op, const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const Div *op, const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const Mod *op, const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const Min *op, const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const Max *op, const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}

    StmtOrExpr make_branch_content(const EQ *op,  const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const NE *op,  const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const LT *op,  const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const LE *op,  const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const GT *op,  const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const GE *op,  const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const And *op, const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}
    StmtOrExpr make_branch_content(const Or *op,  const vector<StmtOrExpr> &ab) {return binary_op_branch_content(op, ab);}

    template<class Op>
    void visit_binary_op(const Op *op) {
        vector<Branch> a_branches;
        collect(op->a, a_branches);

        vector<Branch> b_branches;
        collect(op->b, b_branches);

        merge_child_branches(op, vec(a_branches, b_branches));
    }

    template<class Op, class Cmp>
    void visit_min_or_max(const Op *op) {
        visit_binary_op(op);

        if (!branching_vars) {
            vector<Branch> child_branches;
            branches.swap(child_branches);
            for (size_t i = 0; i < child_branches.size(); ++i) {
                Branch &branch = child_branches[i];
                const Op *min_or_max = branch.content.as<Op>();
                if (min_or_max) {
                    Expr a = min_or_max->a;
                    Expr b = min_or_max->b;
                    if (expr_uses_var(a, name, scope) || expr_uses_var(b, name, scope)) {
                        push_bounds(branch.min, branch.extent);
                        Expr cond = Cmp::make(a, b);
                        if (!visit_simple_cond(cond, a, b)) {
                            branches.push_back(branch);
                        }
                        pop_bounds();

                        continue;
                    }
                }

                // We did not branch, so add current branch as is.
                branches.push_back(branch);
            }
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

    void visit(const Not *op) {
        vector<Branch> a_branches;
        collect(op->a, a_branches);
        for (size_t i = 0; i < a_branches.size(); ++i) {
            Branch &branch = a_branches[i];
            Expr a = branch.content;
            if (a.same_as(op->a)) {
                branch.content = Expr(op);
            } else {
                branch.content = Not::make(a);
            }
            branches.push_back(branch);
        }
    }

    StmtOrExpr make_branch_content(const Select *op, const vector<StmtOrExpr> &args) {
        return Select::make(args[0], args[1], args[2]);
    }

    void visit(const Select *op) {
        if (op->condition.type().is_scalar() && expr_is_linear_in_var(op->condition, name, linearity)) {
            Expr normalized = normalize_branch_conditions(op, name, scope, bounds_info, free_vars, branching_limit);
            const Select *select = normalized.as<Select>();

            if (!select || !visit_simple_cond(select->condition, select->true_value, select->false_value)) {
                add_branch(Expr(op));
            }
        } else {
            vector<Branch> cond_branches;
            collect(op->condition, cond_branches);

            vector<Branch> true_branches;
            collect(op->true_value, true_branches);

            vector<Branch> false_branches;
            collect(op->false_value, false_branches);

            merge_child_branches(op, vec(cond_branches, true_branches, false_branches));
        }
    }

    void visit(const Load *op) {
        vector<Branch> index_branches;
        collect(op->index, index_branches);
        for (size_t i = 0; i < index_branches.size(); ++i) {
            Branch &branch = index_branches[i];
            Expr index = branch.content;
            if (index.same_as(op->index)) {
                branch.content = Expr(op);
            } else {
                branch.content = Load::make(op->type, op->name, index, op->image, op->param);
            }
            branches.push_back(branch);
        }
    }

    StmtOrExpr make_branch_content(const Ramp *op, const vector<StmtOrExpr> &args) {
        return Ramp::make(args[0], args[1], op->width);
    }

    void visit(const Ramp *op) {
        vector<Branch> base_branches;
        collect(op->base, base_branches);

        vector<Branch> stride_branches;
        collect(op->stride, stride_branches);

        merge_child_branches(op, vec(base_branches, stride_branches));
    }

    void visit(const Broadcast *op) {
        vector<Branch> value_branches;
        collect(op->value, value_branches);
        for (size_t i = 0; i < value_branches.size(); ++i) {
            Branch &branch = value_branches[i];
            Expr value = branch.content;
            if (value.same_as(op->value)) {
                branch.content = Expr(op);
            } else {
                branch.content = Broadcast::make(value, op->width);
            }
            branches.push_back(branch);
        }
    }

    StmtOrExpr make_branch_content(const Call *op, const vector<StmtOrExpr> &branch_args) {
        vector<Expr> args;
        for (size_t i = 0; i < branch_args.size(); ++i) {
            args.push_back(branch_args[i]);
        }
        return Call::make(op->type, op->name, args, op->call_type,
                          op->func, op->value_index, op->image, op->param);
    }

    void visit(const Call *op) {
        if (op->args.size() > 0) {
            vector<vector<Branch> > arg_branches(op->args.size());
            for (size_t i = 0; i < op->args.size(); ++i) {
                collect(op->args[i], arg_branches[i]);
            }
            merge_child_branches(op, arg_branches);
        } else {
            Branch branch = {curr_min.top(), curr_extent.top(), Expr(op)};
            branches.push_back(branch);
        }
    }

    template<class LetOp>
    void visit_let(const LetOp *op) {
        // First we branch the value of the let if necessary.
        if (branches_linearly_in_var(op->value, name, linearity, let_num_branches, true)) {
            vector<Branch> value_branches;
            collect(op->value, value_branches);
            for (size_t i = 0; i < value_branches.size(); ++i) {
                Branch &branch = value_branches[i];
                string new_name = op->name + "." + int_to_string(i);
                Expr value = branch.content;
                int value_linearity = expr_linearity(value, free_vars, linearity);
                linearity.push(new_name, value_linearity);
                scope.push(new_name, value);
            }

            vector<Branch> body_branches;
            let_branches.push(op->name, value_branches);
            let_num_branches.push(op->name, value_branches.size()-1);
            collect(op->body, body_branches);
            for (size_t i = 0; i < body_branches.size(); ++i) {
                Branch &branch = body_branches[i];
                // Add all the value branch let bindings.
                for (size_t j = 0; j < value_branches.size(); ++j) {
                    Branch &val_branch = value_branches[j];
                    string new_name = op->name + "." + int_to_string(j);
                    branch.content = LetOp::make(new_name, val_branch.content, branch.content);
                }
                // Add back the original let binding as well, in case we had to bailout somewhere.
                branch.content = LetOp::make(op->name, op->value, branch.content);
                branches.push_back(branch);
            }
            let_branches.pop(op->name);
            let_num_branches.pop(op->name);

            for (size_t i = 0; i < value_branches.size(); ++i) {
                string new_name = op->name + "." + int_to_string(i);
                linearity.pop(new_name);
                scope.pop(new_name);
            }
        } else {
            int value_linearity = expr_linearity(op->value, free_vars, linearity);
            linearity.push(op->name, value_linearity);
            scope.push(op->name, op->value);

            vector<Branch> body_branches;
            collect(op->body, body_branches);
            for (size_t i = 0; i < body_branches.size(); ++i) {
                Branch &branch = body_branches[i];
                branch.content = LetOp::make(op->name, op->value, branch.content);
                branches.push_back(branch);
            }

            linearity.pop(op->name);
            scope.pop(op->name);
        }
    }

    void visit(const Let *op) {visit_let(op);}
    void visit(const LetStmt *op) {visit_let(op);}

    StmtOrExpr make_branch_content(const Pipeline *op, const vector<StmtOrExpr> &args) {
        // If the produce stage is undefined, then assert that there is no update stage,
        // and just return the consume stage.
        if (!args[0].defined()) {
            internal_assert(!args[1].defined());
            return args[2];
        } else {
            return Pipeline::make(op->name, args[0], args[1], args[2]);
        }
    }

    void visit(const Pipeline *op) {
        vector<Branch> produce_branches;
        collect(op->produce, produce_branches);

        vector<Branch> update_branches;
        if (op->update.defined()) {
            collect(op->update, update_branches);
        } else {
            Branch branch = {curr_min.top(), curr_extent.top(), op->update};
            update_branches.push_back(branch);
        }

        vector<Branch> consume_branches;
        collect(op->consume, consume_branches);

        merge_child_branches(op, vec(produce_branches, update_branches, consume_branches));
    }

    StmtOrExpr make_branch_content(const For *op, const vector<StmtOrExpr> &args) {
        // If the loop body is undefined, then just return an undefined Stmt.
        if (!args[2].defined()) {
            return Stmt();
        }

        return For::make(op->name, args[0], args[1], op->for_type, args[2]);
    }

    void visit(const For *op) {
        free_vars.push(op->name, 0);
        vector<Branch> min_branches;
        collect(op->min, min_branches);

        vector<Branch> extent_branches;
        collect(op->extent, extent_branches);

        vector<Branch> body_branches;
        collect(op->body, body_branches);

        merge_child_branches(op, vec(min_branches, extent_branches, body_branches));
        free_vars.pop(op->name);
    }

    StmtOrExpr make_branch_content(const Store *op, const vector<StmtOrExpr> &args) {
        return Store::make(op->name, args[0], args[1]);
    }

    void visit(const Store *op) {
        vector<Branch> value_branches;
        collect(op->value, value_branches);

        vector<Branch> index_branches;
        collect(op->index, index_branches);

        merge_child_branches(op, vec(value_branches, index_branches));
    }

    StmtOrExpr make_branch_content(const Allocate *op, const vector<StmtOrExpr> &args) {
        vector<Expr> extents(args.size()-2);
        size_t i;
        for (i = 0; i < args.size()-2; ++i) {
            extents[i] = args[i];
        };

        return Allocate::make(op->name, op->type, extents, simplify(args[i], true, bounds_info), args[i+1]);
    }

    void visit(const Allocate *op) {
        vector<vector<Branch> > child_branches(op->extents.size());
        for (size_t i = 0; i < op->extents.size(); ++i) {
            collect(op->extents[i], child_branches[i]);
        }

        Expr cond = simplify(op->condition, true, bounds_info);
        Branch branch = {curr_min.top(), curr_extent.top(), op->condition};
        child_branches.push_back(vec(branch));

        vector<Branch> body_branches;
        collect(op->body, body_branches);
        child_branches.push_back(body_branches);

        merge_child_branches(op, child_branches);
    }

    StmtOrExpr make_branch_content(const Block *op, const vector<StmtOrExpr> &args) {
        if (!args[0].defined()) {
            return args[1];
        } else {
            return Block::make(args[0], args[1]);
        }
    }

    void visit(const Block *op) {
        vector<Branch> first_branches;
        collect(op->first, first_branches);

        vector<Branch> rest_branches;
        if (op->rest.defined()) {
            collect(op->rest, rest_branches);
        } else {
            Branch branch = {curr_min.top(), curr_extent.top(), op->rest};
            rest_branches.push_back(branch);
        }

        merge_child_branches(op, vec(first_branches, rest_branches));
    }

    StmtOrExpr make_branch_content(const IfThenElse *op, const vector<StmtOrExpr> &args) {
        return IfThenElse::make(args[0], args[1], args[2]);
    }

    void visit(const IfThenElse *op) {
        if (expr_is_linear_in_var(op->condition, name, linearity)) {
            Stmt normalized = normalize_branch_conditions(op, name, scope, bounds_info, free_vars, branching_limit);
            const IfThenElse *if_stmt = normalized.as<IfThenElse>();

            if (!if_stmt || !visit_simple_cond(if_stmt->condition, if_stmt->then_case, if_stmt->else_case)) {
                add_branch(Stmt(op));
            }
        } else {
            vector<Branch> cond_branches;
            collect(op->condition, cond_branches);

            vector<Branch> then_branches;
            collect(op->then_case, then_branches);

            vector<Branch> else_branches;
            if (op->else_case.defined()) {
                collect(op->else_case, else_branches);
            } else {
                Branch else_branch = {curr_min.top(), curr_extent.top(), op->else_case};
                else_branches.push_back(else_branch);
            }

            merge_child_branches(op, vec(cond_branches, then_branches, else_branches));
        }
    }

    void visit(const Evaluate *op) {
        vector<Branch> value_branches;
        collect(op->value, value_branches);
        for (size_t i = 0; i < value_branches.size(); ++i) {
            Branch &branch = value_branches[i];
            Expr value = branch.content;
            if (value.same_as(op->value)) {
                branch.content = Stmt(op);
            } else {
                branch.content = Evaluate::make(value);
            }
            branches.push_back(branch);
        }
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
    // debug(0) << "Specializing branched loops on Stmt:\n" << s << "\n";
    s = SpecializeBranchedLoops().mutate(s);
    // debug(0) << "Specialized Stmt:\n" << s << "\n\n";
    return s;
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
    string var;
    int num_loops;

    CountLoops(const string &v) : var(v), num_loops(0) {}
};

int count_loops(Stmt stmt, const string &var) {
    CountLoops counter(var);
    stmt.accept(&counter);
    return counter.num_loops;
}

void check_num_branches(Stmt stmt, const string &var, int expected_loops) {
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
    vector<string> bound_vars;

    Expr wrap_in_scope(Expr expr) const {
        for (int i = (int)bound_vars.size() - 1; i >= 0; --i) {
            const string &var = bound_vars[i];
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
    string var;
    const Interval* ival;
    int  index;
    bool matches;

    CheckIntervals(const string& v, const Interval* i ) :
            var(v), ival(i), index(0), matches(true)
    {}
};

void check_branch_intervals(Stmt stmt, const string& loop_var,
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
        //
        // NOTE: We can solve this now. Will leave this test here for
        // future reference.
        Expr cond = !Cast::make(UInt(1), Select::make(x == 0 || x > 5, 0, 1));
        Stmt branch = IfThenElse::make(cond, Store::make("out", 1, x),
                                       Store::make("out", 0, x));
        Stmt stmt = For::make("x", 0, 10, For::Serial, branch);
        Stmt branched = specialize_branched_loops(stmt);
        check_num_branches(branched, "x", 3);
    }

    {
        // Test that we don't touch parallel loops - it would
        // partially serialize them.
        Expr cond = x > 5;
        Stmt branch = IfThenElse::make(cond, Store::make("out", 1, x),
                                       Store::make("out", 0, x));
        Stmt stmt = For::make("x", 0, 10, For::Parallel, branch);
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
