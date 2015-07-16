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

/** Test if an expression references the given variable. */
EXPORT bool expr_uses_var(Expr e, const std::string &v);

/** Test if an expression references the given variable, additionally
 *  considering variables bound to Expr's in the scope provided in the
 *  final argument.
 */
EXPORT bool expr_uses_var(Expr e, const std::string &v, const Scope<Expr> &s);

template<typename T>
class ExprUsesVars : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    const Scope<T> &vars;
    Scope<Expr> scope;

    void visit(const Variable *v) {
        if (vars.contains(v->name)) {
            result = true;
        } else if (scope.contains(v->name)) {
            include(scope.get(v->name));
        }
    }
public:
    ExprUsesVars(const Scope<T> &v, const Scope<Expr> *s = NULL) : vars(v), result(false) {
        scope.set_containing_scope(s);
    }
    bool result;
};

/** Test if an expression references any of the variables in a scope. */
template<typename T>
inline bool expr_uses_vars(Expr e, const Scope<T> &v) {
    ExprUsesVars<T> uses(v);
    e.accept(&uses);
    return uses.result;
}

/** Test if an expression references any of the variables in a scope, additionally
 *  considering variables bound to Expr's in the scope provided in the final argument.
 */
template<typename T>
inline bool expr_uses_vars(Expr e, const Scope<T> &v, const Scope<Expr> &s) {
    ExprUsesVars<T> uses(v, &s);
    e.accept(&uses);
    return uses.result;
}

}
}

#endif
