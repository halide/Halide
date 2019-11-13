#ifndef HALIDE_EXPR_USES_VAR_H
#define HALIDE_EXPR_USES_VAR_H

/** \file
 * Defines a method to determine if an expression depends on some variables.
 */

#include "IR.h"
#include "IRVisitor.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

template<typename T = void>
class ExprUsesVars : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    const Scope<T> &vars;
    Scope<Expr> scope;

    void include(const Expr &e) override {
        if (result) return;
        IRGraphVisitor::include(e);
    }

    void include(const Stmt &s) override {
        if (result) return;
        IRGraphVisitor::include(s);
    }

    void visit_name(const std::string &name) {
        if (vars.contains(name)) {
            result = true;
        } else if (scope.contains(name)) {
            include(scope.get(name));
        }
    }

    void visit(const Variable *op) override {
        visit_name(op->name);
    }

    void visit(const Load *op) override {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }

    void visit(const Store *op) override {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }

    void visit(const Call *op) override {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }

    void visit(const Provide *op) override {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }

    void visit(const LetStmt *op) override {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }

    void visit(const Let *op) override {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }

    void visit(const Realize *op) override {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }

    void visit(const Allocate *op) override {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }

public:
    ExprUsesVars(const Scope<T> &v, const Scope<Expr> *s = nullptr)
        : vars(v), result(false) {
        scope.set_containing_scope(s);
    }
    bool result;
};

/** Test if a statement or expression references or defines any of the
 *  variables in a scope, additionally considering variables bound to
 *  Expr's in the scope provided in the final argument.
 */
template<typename StmtOrExpr, typename T>
inline bool stmt_or_expr_uses_vars(StmtOrExpr e, const Scope<T> &v,
                                   const Scope<Expr> &s = Scope<Expr>::empty_scope()) {
    ExprUsesVars<T> uses(v, &s);
    e.accept(&uses);
    return uses.result;
}

/** Test if a statement or expression references or defines the given
 * variable, additionally considering variables bound to Expr's in the
 * scope provided in the final argument.
 */
template<typename StmtOrExpr>
inline bool stmt_or_expr_uses_var(StmtOrExpr e, const std::string &v,
                                  const Scope<Expr> &s = Scope<Expr>::empty_scope()) {
    Scope<> vars;
    vars.push(v);
    return stmt_or_expr_uses_vars<StmtOrExpr, void>(e, vars, s);
}

/** Test if an expression references or defines the given variable,
 *  additionally considering variables bound to Expr's in the scope
 *  provided in the final argument.
 */
inline bool expr_uses_var(Expr e, const std::string &v,
                          const Scope<Expr> &s = Scope<Expr>::empty_scope()) {
    return stmt_or_expr_uses_var(e, v, s);
}

/** Test if a statement references or defines the given variable,
 *  additionally considering variables bound to Expr's in the scope
 *  provided in the final argument.
 */
inline bool stmt_uses_var(Stmt stmt, const std::string &v,
                          const Scope<Expr> &s = Scope<Expr>::empty_scope()) {
    return stmt_or_expr_uses_var(stmt, v, s);
}

/** Test if an expression references or defines any of the variables
 *  in a scope, additionally considering variables bound to Expr's in
 *  the scope provided in the final argument.
 */
template<typename T>
inline bool expr_uses_vars(Expr e, const Scope<T> &v,
                           const Scope<Expr> &s = Scope<Expr>::empty_scope()) {
    return stmt_or_expr_uses_vars(e, v, s);
}

/** Test if a statement references or defines any of the variables in
 *  a scope, additionally considering variables bound to Expr's in the
 *  scope provided in the final argument.
 */
template<typename T>
inline bool stmt_uses_vars(Stmt stmt, const Scope<T> &v,
                           const Scope<Expr> &s = Scope<Expr>::empty_scope()) {
    return stmt_or_expr_uses_vars(stmt, v, s);
}

}  // namespace Internal
}  // namespace Halide

#endif
