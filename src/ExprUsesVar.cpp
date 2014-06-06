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

}

bool expr_uses_var(Expr e, const string &v) {
    ExprUsesVar uses(v);
    e.accept(&uses);
    return uses.result;
}

}
}
