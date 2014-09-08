#include "SpecializeClampedRamps.h"
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
    Stmt stmt;
};

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
    return NegateExpr().mutate(cond);
}

class NormalizeIfStmts : public IRMutator {
public:
    NormalizeIfStmts() {}

private:
    using IRVisitor::visit;

    Expr cond;
    Stmt then_case;
    Stmt else_case;

    void visit(const IfThenElse *op) {
        then_case = op->then_case;
        else_case = op->else_case;
        cond = mutate(op->condition);
        stmt = IfThenElse::make(cond, mutate(then_case), mutate(else_case));
        if (!cond.same_as(op->condition)) {
            stmt = mutate(stmt);
        }
    }

    void visit(const Not *op) {
        expr = mutate(negate(op->a));
    }

    void visit(const And *op) {
        then_case = IfThenElse::make(op->b, then_case, else_case);
        expr = op->a;
    }

    void visit(const Or *op) {
        else_case = IfThenElse::make(op->b, then_case, else_case);
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

    Expr cond;
    Expr true_value;
    Expr false_value;

    void visit(const Select *op) {
        true_value = op->true_value;
        false_value = op->false_value;
        cond = mutate(op->condition);
        expr = Select::make(cond, mutate(true_value), mutate(false_value));
        if (!cond.same_as(op->condition)) {
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

// class BranchConditionalExpr : public IRVisitor {
// public:
//     std::string name;
//     std::vector<Branch>* branches;
//     Scope<Expr>* scope;
//     Expr min;
//     Expr extent;

//     Expr true_value;
//     Expr false_value;

//     BranchConditionalExpr(const std::string& n, std::vector<Branch>* b,
//                           Scope<Expr>* s, Expr m, Expr e, Expr t, Expr f) :
//         name(n), branches(b), scope(s), min(m), extent(e), true_value(t), false_value(f) {}
// private:
//     using IRVisitor::visit;

//     void visit(const Not *op) {
//         Expr cond = negate(op->a);
//         cond.accept(this);
//     }

//     void visit(const And *op) {
//         true_value = Select::make(op->b, true_value, false_value);
//         op->a.accept(this);
//     }

//     void visit(const Or *op) {
//         false_value = Select::make(op->b, true_value, false_value);
//         op->a.accept(this);
//     }

//     template<class Op>
//     void visit_compare(const Op *op) {
//         Expr cond = solve_for_linear_variable(op, name);
//         if (!cond.same_as(op)) {
//             Branch b1, b2;
//             build_branches(cond, true_value, false_value, b1, b2);

//             int num_branches = branches.size();
//             int orig_min = min;
//             int orig_extent = extent;

//             min = b1.min;
//             extent = b1.extent;
//             b1.expr.accept(this);

//             min = b2.min;
//             extent = b2.extent;
//             b2.expr.accept(this);

//             // If we didn't branch any further, push these branches onto the stack.
//             if (branches.size() == num_branches) {
//                 branches.push_back(b1);
//                 branches.push_back(b2);
//             }

//             min = orig_min;
//             extent = orig_extent;
//         }
//     }

//     void visit(const LT *op) {visit_compare<LT>(op);}
//     void visit(const LE *op) {visit_compare<LE>(op);}
//     void visit(const GT *op) {visit_compare<GT>(op);}
//     void visit(const GE *op) {visit_compare<GE>(op);}
// };

class BranchesInVar : public IRVisitor {
public:
    std::string name;
    bool has_branches;

    BranchesInVar(const std::string& n) : name(n), has_branches(false) {}

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
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Max *op) {
        if (expr_uses_var(op->a, name) ||
            expr_uses_var(op->b, name)) {
            has_branches = true;
        } else {
            IRVisitor::visit(op);
        }
    }
};

bool stmt_branches_in_var(const std::string& name, Stmt body) {
    BranchesInVar check(name);
    body.accept(&check);
    return check.has_branches;
}

bool expr_branches_in_var(const std::string& name, Expr value) {
    BranchesInVar check(name);
    value.accept(&check);
    return check.has_branches;
}

void collect_branches(Expr expr, const std::string& name, Expr min, Expr extent, std::vector<Branch> &branches, Scope<Expr>* scope);
void collect_branches(Stmt stmt, const std::string& name, Expr min, Expr extent, std::vector<Branch> &branches, Scope<Expr>* scope);

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

            b2.min = le->b + 1;
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
            Expr ext1 = simplify(ge->b - min);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.expr = b;

            b2.min = ge->b;
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

            b2.min = le->b + 1;
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
            Expr ext1 = simplify(ge->b - min);
            Expr ext2 = simplify(extent - ext1);

            b1.min = min;
            b1.extent = ext1;
            b1.stmt = b;

            b2.min = ge->b;
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

    void visit_simple_cond(Expr cond, Expr a, Expr b) {
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

    void visit(const LetStmt *op) {
        if (expr_branches_in_var(name, op->value)) {
            std::vector<Branch> expr_branches;
            collect_branches(op->value, name, min, extent, expr_branches, scope);

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
                    branches.push_back(branch);
                }
                num_branches = branches.size();
            }

            min = orig_min;
            extent = orig_extent;
        } else {
            scope->push(op->name, op->value);
            op->body.accept(this);
            scope->pop(op->name);
        }
    }

    void visit(const Store *op) {
        if (expr_branches_in_var(name, op->value)) {
            std::vector<Branch> expr_branches;
            collect_branches(op->value, name, min, extent, expr_branches, scope);

            for (size_t i = 0; i < expr_branches.size(); ++i) {
                Branch &branch = expr_branches[i];
                branch.stmt = Store::make(op->name, branch.expr, op->index);
                branch.expr = Expr();
                branches.push_back(branch);
            }
        }
    }

    void visit(const IfThenElse *op) {
        Expr solve = solve_for_linear_variable(op->condition, name, scope);

        if (!solve.same_as(op->condition)) {
            Branch b1, b2;
            build_branches(solve, op->then_case, op->else_case, b1, b2);

            size_t num_branches = branches.size();
            Expr orig_min = min;
            Expr orig_extent = extent;

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

};

void collect_branches(Expr expr, const std::string& name, Expr min, Expr extent,
                      std::vector<Branch> &branches, Scope<Expr>* scope) {
    BranchCollector collector(name, min, extent, scope);
    expr.accept(&collector);
    branches.swap(collector.branches);
}

void collect_branches(Stmt stmt, const std::string& name, Expr min, Expr extent,
                      std::vector<Branch> &branches, Scope<Expr>* scope) {
    BranchCollector collector(name, min, extent, scope);
    stmt.accept(&collector);
    branches.swap(collector.branches);
}

class SpecializeBranchedLoops : public IRMutator {
private:
    using IRVisitor::visit;

    Scope<Expr> scope;

    void visit(const For *op) {
        if (stmt_branches_in_var(op->name, op->body)) {
            Stmt loop_body = normalize_if_stmts(op->body);
            loop_body = normalize_selects(loop_body);

            std::vector<Branch> branches;
            collect_branches(loop_body, op->name, op->min, op->extent, branches, &scope);

            Branch &last = branches.back();
            stmt = For::make(op->name, last.min, last.extent, op->for_type, mutate(last.stmt));

            for (int i = branches.size()-2; i >= 0; --i) {
                Branch &next = branches[i];
                stmt = Block::make(For::make(op->name, next.min, next.extent, op->for_type, mutate(next.stmt)), stmt);
            }
        } else {
            stmt = For::make(op->name, op->min, op->extent, op->for_type, mutate(op->body));
        }
    }

    void visit(const LetStmt *op) {
        scope.push(op->name, op->value);
        mutate(op->body);
        scope.pop(op->name);
    }
};

Stmt specialize_branched_loops(Stmt s) {
#if 1
    SpecializeBranchedLoops specialize;

    std::cout << "Specializing branched loops in stmt:\n" << s << std::endl
              << "======================================================\n";
    Stmt ss = specialize.mutate(s);
    std::cout << ss << "======================================================\n";
    return ss;
#else
    return SpecializeBranchedLoops().mutate(s);
#endif
}

}
}
