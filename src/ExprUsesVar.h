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

template<typename T>
class ExprUsesVars : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    const Scope<T> &vars;
    Scope<Expr> scope;

    void visit_name(const std::string &name) {
        if (vars.contains(name)) {
            result = true;
        } else if (scope.contains(name)) {
            include(scope.get(name));
        }
    }

    void visit(const Variable *op) {
        visit_name(op->name);
    }

    void visit(const Load *op) {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }

    void visit(const Store *op) {
        visit_name(op->name);
        IRGraphVisitor::visit(op);
    }
public:
    ExprUsesVars(const Scope<T> &v, const Scope<Expr> *s = nullptr) : vars(v), result(false) {
        scope.set_containing_scope(s);
    }
    bool result;
};

/** Test if a statement or expression references the given variable. */
template<typename StmtOrExpr>
inline bool stmt_or_expr_uses_var(StmtOrExpr e, const std::string &v) {
    Scope<int> s;
    s.push(v, 0);
    ExprUsesVars<int> uses(s);
    e.accept(&uses);
    return uses.result;
}

/** Test if a statement or expression references any of the variables
 *  in a scope, additionally considering variables bound to Expr's in
 *  the scope provided in the final argument.
 */
template<typename StmtOrExpr, typename T>
inline bool stmt_or_expr_uses_vars(StmtOrExpr e, const Scope<T> &v,
                                   const Scope<Expr> &s = Scope<Expr>::empty_scope()) {
    ExprUsesVars<T> uses(v, &s);
    e.accept(&uses);
    return uses.result;
}

/** Test if an expression references the given variable. */
inline bool expr_uses_var(Expr e, const std::string &v) {
    return stmt_or_expr_uses_var(e, v);
}

/** Test if a statement references the given variable. */
inline bool stmt_uses_var(Stmt s, const std::string &v) {
    return stmt_or_expr_uses_var(s, v);
}

/** Test if an expression references any of the variables in a scope,
 *  additionally considering variables bound to Expr's in the scope
 *  provided in the final argument.
 */
template<typename T>
inline bool expr_uses_vars(Expr e, const Scope<T> &v,
                           const Scope<Expr> &s = Scope<Expr>::empty_scope()) {
    return stmt_or_expr_uses_vars(e, v, s);
}

/** Test if a statement references any of the variables in a scope,
 *  additionally considering variables bound to Expr's in the scope
 *  provided in the final argument.
 */
template<typename T>
inline bool stmt_uses_vars(Stmt e, const Scope<T> &v,
                           const Scope<Expr> &s = Scope<Expr>::empty_scope()) {
    return stmt_or_expr_uses_vars(e, v, s);
}

}
}

#endif
