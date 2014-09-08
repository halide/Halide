#include "SpecializeClampedRamps.h"
#include "BranchedLoopsGrid.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LinearSolve.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

namespace {

bool is_inequality(Expr cond) {
    const LT *lt = cond.as<LT>();
    const GT *gt = cond.as<GT>();
    const LE *le = cond.as<LE>();
    const GE *ge = cond.as<GE>();
    
    return lt || gt || le || ge;
}

}

struct Branch {
    Expr min;
    Expr extent;
    Expr expr;
};

// Build a pair of branches for 2 exprs, based on a simple inequality conditional.
// It is assumed that the inequality has been solved and so the variable of interest
// is on the left hand side.
void build_branches(Expr cond, Expr a, Expr b, Branch &b1, Branch &b2) {
    const LE *le = solve.as<LE>();
    const GE *ge = solve.as<GE>();
    const LT *lt = solve.as<LT>();
    const GT *gt = solve.as<GT>();
                
    if (le) {
        Expr ext1 = simplifiy(le->b - min + 1);
        Expr ext2 = simplifiy(extent - ext1);
                    
        b1.min = min;
        b1.extent = ext1;
        b1.expr = a;
                    
        b2.min = le->b + 1;
        b2.extent = ext2;
        b2.expr = b;
    } else if(lt) {
        Expr ext1 = simplifiy(lt->b - min);
        Expr ext2 = simplifiy(extent - ext1);
                    
        b1.min = min;
        b1.extent = ext1;
        b1.expr = a;
                    
        b2.min = lt->b;
        b2.extent = ext2;
        b2.expr = b;
    } else if(ge) {
        Expr ext1 = simplifiy(ge->b - min);
        Expr ext2 = simplifiy(extent - ext1);
                    
        b1.min = min;
        b1.extent = ext1;
        b1.expr = b;
                    
        b2.min = ge->b;
        b2.extent = ext2;
        b2.expr = a;
    } else if(gt) {
        Expr ext1 = simplifiy(gt->b - min);
        Expr ext2 = simplifiy(extent - ext1);
                    
        b1.min = min;
        b1.extent = ext1;
        b1.expr = b;
                    
        b2.min = gt->b;
        b2.extent = ext2;
        b2.expr = a;
    }
}
 
class NegateExpr : public IRMutator {
public:
    NegateExpr() {}

private:
    using IRVisitor::visit;

    void visit(const Not *op) {
        expr = mutate(op->a);
    }

    template<class Op, class Nop>
    void visit_binary(const Op *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        expr = Nop::make(a, b);
    }

    void visit(const And *op) {visit_binary<And, Or>(op);}
    void visit(const Or *op)  {visit_binary<Or, And>(op);}
    void visit(const EQ *op)  {visit_binary<EQ, NE>(op);}
    void visit(const NE *op)  {visit_binary<NE, EQ>(op);}
    void visit(const GT *op)  {visit_binary<GT, LE>(op);}
    void visit(const LT *op)  {visit_binary<LT, GE>(op);}
    void visit(const GE *op)  {visit_binary<GE, LT>(op);}
    void visit(const LE *op)  {visit_binary<LE, GT>(op);}
};

Expr negate(Expr cond) {
    return NegateConditional().mutate(cond);
}
 
class NormalizeIfStmts : public IRMutator {
public:
    NormalizeIfStmts() {}

private:
    using IRVisitor::visit;

    Stmt then_stmt;
    Stmt else_stmt;

    void visit(const IfThenElse *op) {
        then_stmt = op->then_stmt;
        else_stmt = op->else_stmt;
        cond = mutate(op->cond);
        stmt = IfThenElse::make(cond, mutate(then_stmt), mutate(else_stmt));
        if (!cond.same_as(op->cond)) {
            stmt = mutate(stmt);
        }
    }

    void visit(const Not *op) {
        expr = mutate(negate(op->a));
    }

    void visit(const And *op) {
        then_stmt = IfThenElse::make(op->b, then_stmt, else_stmt);
        expr = op->a;
    }

    void visit(const Or *op) {
        else_stmt = IfThenElse::make(op->b, then_stmt, else_stmt);
        expr = op->a;
    }
};

Stmt normalize_if_stmts(Stmt stmt) {
    return NormalizeIfStmts().mutate(stmt);
}
 
class NormalizeSelects : public IRMutator {
public:
    NormalizeSelects() {}

private:
    using IRVisitor::visit;

    Expr true_value;
    Expr false_value;

    void visit(const Select *op) {
        true_value = op->true_value;
        false_value = op->false_value;
        cond = mutate(op->cond);
        expr = Select::make(cond, mutate(true_value), mutate(false_value));
        if (!cond.same_as(op->cond)) {
            expr = mutate(expr);
        }
    }

    void visit(const Not *op) {
        expr = mutate(negate(op->a));
    }

    void visit(const And *op) {
        true_value = Select::make(op->b, true_value, false_value);
        expr = op->a;
    }

    void visit(const Or *op) {
        false_value = Select::make(op->b, true_value, false_value);
        expr = op->a;
    }
};

Stmt normalize_selects(Stmt stmt) {
    return NormalizeSelects().mutate(stmt);
}

class BranchConditionalExpr : public IRVisitor {
public:
    std::string name;
    std::vector<Branch>* branches;
    Scope<Expr>* scope;
    Expr min;
    Expr extent;

    Expr true_value;
    Expr false_value;

    BranchConditionalExpr(const std::string& n, std::vector<Branch>* b,
                          Scope<Expr>* s, Expr m, Expr e, Expr t, Expr f) :
        name(n), branches(b), scope(s), min(m), extent(e), true_value(t), false_value(f) {}
private:
    using IRVisitor::visit;

    void visit(const Not *op) {
        Expr cond = negate(op->a);
        cond.accept(this);
    }

    void visit(const And *op) {
        true_value = Select::make(op->b, true_value, false_value);
        op->a.accept(this);
    }

    void visit(const Or *op) {
        false_value = Select::make(op->b, true_value, false_value);
        op->a.accept(this);
    }

    template<class Op>
    void visit_compare(const Op *op) {
        Expr cond = solve_for_linear_variable(op, name);
        if (!cond.same_as(op)) {
            Branch b1, b2;
            build_branches(cond, true_value, false_value, b1, b2);

            int num_branches = branches.size();
            int orig_min = min;
            int orig_extent = extent;

            min = b1.min;
            extent = b1.extent;
            b1.expr.accept(this);

            min = b2.min;
            extent = b2.extent;
            b2.expr.accept(this);

            // If we didn't branch any further, push these branches onto the stack.
            if (branches.size() == num_branches) {
                branches.push_back(b1);
                branches.push_back(b2);
            }

            min = orig_min;
            extent = orig_extent;
        }
    }

    void visit(const LT *op) {visit_compare<LT>(op);}
    void visit(const LE *op) {visit_compare<LE>(op);}
    void visit(const GT *op) {visit_compare<GT>(op);}
    void visit(const GE *op) {visit_compare<GE>(op);}
};

class BranchCollector : public IRVisitor {
public:
    std::string name;
    std::vector<Branch> branches;
    Scope<Expr>* scope;
    Expr min;
    Expr extent;

    BranchCollector(const std::string& n, Expr m, Expr e, Scope<Expr>* s) :
        name(n), scope(s), min(m), extent(e) {}

private:
    using IRVisitor::visit;

    void visit_simple_cond(Expr cond, Expr a, Expr b) {
        Expr solve = solve_for_linear_variable(cond, name, scope);
            
        if (!solve.same_as(cond)) {
            Branch b1, b2;
            build_branches(solve, op->true_value, op->false_value, b1, b2);
                
            int num_branches = branches.size();
            int orig_min = min;
            int orig_extent = extent;

            min = b1.min;
            extent = b1.extent;
            b1.expr.accept(this);

            min = b2.min;
            extent = b2.extent;
            b2.expr.accept(this);

            // If we didn't branch any further, push these branches onto the stack.
            if (branches.size() == num_branches) {
                branches.push_back(b1);
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

        if (expr_uses_var(a, name) || expr_uses_var(b, name)) {
            Expr cond = Cmp::make(a, b);
            visit_simple_cond(cond, a, b);
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Min *op) {visit_minormax<Min, LE>(op);}
    void visit(const Max *op) {visit_minormax<Max, GE>(op);}

    void visit(const Select *op) {
        if (expr_uses_var(op->condition, name)) {
            visit_simple_cond(op->condition, op->true_value, op->false_value);
        } else {
            IRVisitor::visit(op);
        }
    }
};

void collect_branches(Expr expr, const std::string& name, Expr min, Expr extent, std::vector<Branch> &branches, Scope<Expr>* scope) {
    BranchCollector collector(name, min, extent, scope);
    expr.accept(collector);
    branches.swap(collector.branches());
}

class BranchesInVar : public IRVisitor {
public:
    std::string name;
    bool has_branches;

    BranchesInVar(const std::string& n) : name(n) has_branches(false) {}

private:
    using IRVisitor::visit;

    void visit(const IfThenElse *op) {
        if (expr_uses_var(op->condition, name)) {
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Select *op) {
        if (expr_uses_var(op->condition, name)) {
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Min *op) {
        if (expr_uses_var(op->a, name) ||
            expr_uses_var(op->b, name)) {
            has_bracnhes = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Max *op) {
        if (expr_uses_var(op->a, name) ||
            expr_uses_var(op->b, name)) {
            has_bracnhes = true;
        } else {
            IRVisitor::visit(op);
        }
    }
};
 
bool stmt_branches_in_var(const std::string& name, Stmt body) {
    BranchesInVar check(name);
    body.accept(check);
    return check.has_branches;
}
 
bool expr_branches_in_var(const std::string& name, Expr value) {
    BranchesInVar check(name);
    value.accept(check);
    return check.has_branches;
}

class ConstructLoopBranches : public IRVisitor {
public:
    BranchedLoopsGrid<Stmt> branches;
    
    ConstructLoopBranches() : curr_dim(-1) {}

private:
    using IRVisitor::mutate;
    
    Scope<Expr> scope;
    std::string curr_var;
    int curr_dim;
    std::vector<int> curr_cell;
    std::vector<Expr> min;
    std::vector<Expr> extent;

    void visit(const For *op) {
        branches.push_dim(op->name, op->min, op->extent);
        curr_cell.push_back(0);
        curr_dim++;
        curr_var = op->name;
        op->body.accept(this);
        if (curr_dim < branches.dim()-1) {
            curr_var = op->name;
            
        }
        curr_dim--;
    }
 
    void visit(const LetStmt *op) {
        if (expr_branches_in_var(curr_var, op->value)) {
            std::vector<Branch> expr_branches;
            Expr min = branches.min(curr_dim, curr_cell);
            Expr extent = branches.extent(curr_dim, curr_cell);
            collect_branches(op->value, curr_var, min, extent, expr_branches, scope);
            for (int i = 0; i < expr_branches.size(); ++i) {
                Branch &branch = expr_branches[i];
                if (i < expr_branches.size()-1) {
                    branches.split(curr_dim, curr_cell, simplify(branch.min + branch.extent));
                }
                scope.push(op->name, expr_branches.expr);
                op->body.accept(this);
                scope.pop(op->name);
                branches(curr_cell) = LetStmt::make(op->name, branch.expr, branches(curr_cell));
                curr_cell[curr_dim]++;
            }
        } else {
            scope.push(op->name, op->value);
            op->body.accept(this);
            scope.pop(op->name);
            branches(curr_cell) = LetStmt::make(op->name, branch.expr, branches(curr_cell));
        }
    }
 
    void visit(const Store *op) {
        if (expr_branches_in_var(curr_var, op->value)) {
            std::vector<Branch> expr_branches;
            Expr min = branches.min(curr_dim, curr_cell);
            Expr extent = branches.extent(curr_dim, curr_cell);
            collect_branches(op->value, curr_var, min, extent, expr_branches, scope);
            for (int i = 0; i < expr_branches.size(); ++i) {
                Branch &branch = expr_branches[i];
                if (i < expr_branches.size()-1) {
                    branches.split(curr_dim, curr_cell, simplify(branch.min + branch.extent));
                }
                branches(curr_cell) = Store::make(op->name, branch.expr, index);
                curr_cell[curr_dim]++;
            }
        } else {
            branches(curr_cell) = op;
        }
    }

    void visit(const IfThenElse *op) {
    }
};


#if OLD_CODE
class LoopHasBranches : public IRVisitor {
public:
    std::string name;
    bool has_branches;

    LoopHasBranches(const std::string& n) : name(n) has_branches(false) {}

private:
    using IRVisitor::visit;

    void visit(const IfThenElse *op) {
        if (expr_uses_var(op->condition, name)) {
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Select *op) {
        if (expr_uses_var(op->condition, name)) {
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Min *op) {
        if (expr_uses_var(op->a, name) ||
            expr_uses_var(op->b, name)) {
            has_bracnhes = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Max *op) {
        if (expr_uses_var(op->a, name) ||
            expr_uses_var(op->b, name)) {
            has_bracnhes = true;
        } else {
            IRVisitor::visit(op);
        }
    }
};

bool check_loop_has_branches(const std::string& name, Stmt body) {
    LoopHasBranches check(name);
    body.accept(check);
    return check.has_branches;
}

class CheckBranched : public IRVisitor {
public:
    Scope<Expr>* scope;
    std::vector<BranchedLoop>& loops;

    CheckBranched(std::vector<BranchedLoop>& l, Scope<Expr>* s) : scope(s), loops(l) {}

private:
    using IRVisitor::visit;

    void visit(const IfThenElse *op) {
        for (size_t i = loops.size()-1; i >= 0; --i) {
            const For* for_loop = loops[i].op;
            Expr cond = op->condition;
            if (is_inequality(cond) && expr_uses_var(cond, for_loop->name)) {
                Expr solve = solve_for_linear_variable(cond, for_loop->name, scope);
                if (!solve.same_as(cond)) {
                    const LT *lt = solve.as<LT>();
                    const GT *gt = solve.as<GT>();
                    const LE *le = solve.as<LE>();
                    const GE *ge = solve.as<GE>();

                    if (lt) {
                        Expr lhs = lt->b;
                        Branch b = {for_loop->min, simplify(lhs - for_loop->min),
                                    op->then_case, op->else_case};
                        loops[i].branch = b;
                        loops[i].has_branch = true;
                        break;
                    } else if (gt) {
                        Expr lhs = gt->b;
                        Branch b = {simplify(lhs + 1), for_loop->extent,
                                    op->then_case, op->else_case};
                        loops[i].branch = b;
                        loops[i].has_branch = true;
                        break;
                    } else if (le) {
                        Expr lhs = le->b;
                        Branch b = {for_loop->min, simplify(lhs - for_loop->min),
                                    op->then_case, op->else_case};
                        loops[i].branch = b;
                        loops[i].has_branch = true;
                        break;
                    } else /*if (ge)*/ {
                        Expr lhs = ge->b;
                        Branch b = {lhs, for_loop->extent,
                                    op->then_case, op->else_case};
                        loops[i].branch = b;
                        loops[i].has_branch = true;
                        break;
                    }
                }
            }
        }
    }

    void visit(const LetStmt *op) {
        scope->push(op->name, op->value);
        op->body.accept(this);
        scope->pop(op->name);
    }

};

class SpecializeLoopBranches : public IRMutator {
public:
    SpecializeLoopBranches() : check_loop(loops, &scope) {}
private:
    Scope<Expr> scope;
    std::vector<BranchedLoop> loops;
    CheckBranched check_loop;

    using IRVisitor::visit;

    void visit(const For *op) {
        BranchedLoop loop;
        loop.op = op;
        loop.has_branch = false;
        loops.push_back(loop);

        Stmt body = mutate(op->body);
        body.accept(&check_loop);

        if (loops.back().has_branch) {
            const Branch& branch = loops.back().branch;

            Stmt first;
            Stmt second;

            if (branch.min.same_as(op->min)) {
                first  = For::make(op->name, branch.min, branch.extent, op->for_type, branch.then_case);

                if (branch.else_case.defined()) {
                    Expr second_min = simplify(branch.min + branch.extent);
                    Expr second_extent = simplify(op->extent - branch.extent);
                    second = For::make(op->name, second_min, second_extent, op->for_type, branch.else_case);
                }
            } else {
                if (branch.else_case.defined()) {
                    Expr first_extent = simplify(branch.min - op->min);
                    first  = For::make(op->name, op->min, first_extent, op->for_type, branch.else_case);
                }

                second = For::make(op->name, branch.min, branch.extent, op->for_type, branch.then_case);
            }

            if (first.defined() && second.defined()) {
                stmt = Block::make(first, second);
            } else if (first.defined()) {
                stmt = first;
            } else {
                stmt = second;
            }
        } else {
            stmt = op;
        }

        loops.pop_back();
    }

    void visit(const LetStmt *op) {
        scope.push(op->name, op->value);
        Stmt body = mutate(op->body);
        scope.pop(op->name);

        if (body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, op->value, body);
        }
    }
};

Stmt specialize_branched_loops(Stmt s) {
    Stmt spec_stmt = s;
    SpecializeLoopBranches specialize;
    do {
        s = spec_stmt;
        spec_stmt = specialize.mutate(s);
    } while(!spec_stmt.same_as(s));

    return spec_stmt;
}
#endif

}
}
