#include "InlineReductions.h"
#include "Func.h"
#include "Scope.h"
#include "IRPrinter.h"
#include "IROperator.h"

namespace Halide {

using std::string;
using std::vector;
using std::ostringstream;

namespace Internal {

class FindFreeVars : public IRVisitor {
public:
    vector<Var> free_vars;

private:
    Scope<int> internal;

    using IRVisitor::visit;

    void visit(const Let *op) {
        op->value.accept(this);
        internal.push(op->name, 0);
        op->body.accept(this);
        internal.pop(op->name);
    }

    void visit(const Variable *v) {
        if (internal.contains(v->name)) {
            // Don't capture internally defined vars
            return;
        }

        if (starts_with(v->name, "iv.")) {
            // Don't capture implicit vars
            return;
        }
        
        if (v->reduction_domain.defined()) {
            // Skip reduction variables
            return;
        }

        if (v->param.defined()) {
            // Skip parameters
            return;
        }

        for (size_t i = 0; i < free_vars.size(); i++) {
            if (v->name == free_vars[i].name()) return;
        }

        free_vars.push_back(Var(v->name));
    }
};
}

Expr sum(Expr e) {
    Internal::FindFreeVars v;
    e.accept(&v);
    Func f("sum");
    f(v.free_vars) += e;
    return f(v.free_vars);
}

Expr product(Expr e) {
    Internal::FindFreeVars v;
    e.accept(&v);
    Func f("product");
    f(v.free_vars) *= e;
    return f(v.free_vars);
}

Expr maximum(Expr e) {
    Internal::FindFreeVars v;
    e.accept(&v);
    Func f("maximum");
    f(v.free_vars) = e.type().min();
    f(v.free_vars) = max(f(v.free_vars), e);
    return f(v.free_vars);
}

Expr minimum(Expr e) {
    Internal::FindFreeVars v;
    e.accept(&v);
    Func f("minimum");
    f(v.free_vars) = e.type().max();
    f(v.free_vars) = min(f(v.free_vars), e);
    return f(v.free_vars);
}

}
