#include "ExprUsesVar.h"

namespace Halide {
namespace Internal {

using std::string;

namespace {
class ExprUsesVar : public IRVisitor {
    using IRVisitor::visit;

    const string &var;

    void visit(const Variable *v) {
        if (v->name == var) {
            result = true;
        }
    }
public:
    ExprUsesVar(const string &v) : var(v), result(false) {}
    bool result;
};

class ExprUsesVars : public IRVisitor {
    using IRVisitor::visit;

    const Scope<int> &scope;

    void visit(const Variable *v) {
        if (scope.contains(v->name)) {
            result = true;
        }
    }
public:
    ExprUsesVars(const Scope<int> &s) : scope(s), result(false) {}
    bool result;
};
}

bool expr_uses_var(Expr e, const string &v) {
    ExprUsesVar uses(v);
    e.accept(&uses);
    return uses.result;
}

bool expr_uses_vars(Expr e, const Scope<int> &s) {
    ExprUsesVars uses(s);
    e.accept(&uses);
    return uses.result;
}

}
}
