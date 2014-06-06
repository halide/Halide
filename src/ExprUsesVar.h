#ifndef HALIDE_EXPR_USES_VAR_H
#define HALIDE_EXPR_USES_VAR_H

/** \file
 * Defines a method to determine if an expression depends on some variables.
 */

#include "IR.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Test if an expression references the given variable. */
bool expr_uses_var(Expr e, const std::string &v);

template<typename T>
class ExprUsesVars : public IRVisitor {
    using IRVisitor::visit;

    const Scope<T> &scope;

    void visit(const Variable *v) {
        if (scope.contains(v->name)) {
            result = true;
        }
    }
public:
    ExprUsesVars(const Scope<T> &s) : scope(s), result(false) {}
    bool result;
};

/** Test if an expression references any of the variables in a scope. */
template<typename T>
inline bool expr_uses_vars(Expr e, const Scope<T> &s) {
    ExprUsesVars<T> uses(s);
    e.accept(&uses);
    return uses.result;
}

}
}

#endif
