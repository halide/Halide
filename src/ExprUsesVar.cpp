#include "ExprUsesVar.h"

namespace Halide {
namespace Internal {

using std::string;

namespace {
class ExprUsesVar : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    const string &var;
    Scope<Expr> scope;

    void visit(const Variable *v) {
        if (v->name == var) {
            result = true;
        } else if (scope.contains(v->name)) {
            include(scope.get(v->name));
        }
    }
public:
  ExprUsesVar(const string &v, const Scope<Expr> *s = NULL) : var(v), result(false) {
        scope.set_containing_scope(s);
    }
    bool result;
};
}

bool expr_uses_var(Expr e, const string &v) {
    ExprUsesVar uses(v);
    e.accept(&uses);
    return uses.result;
}

bool expr_uses_var(Expr e, const string &v, const Scope<Expr> &s) {
    ExprUsesVar uses(v, &s);
    e.accept(&uses);
    return uses.result;
}

}
}
