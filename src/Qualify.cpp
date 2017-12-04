#include "Qualify.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::string;

// Prefix all names in an expression with some string.
class QualifyExpr : public IRMutator {
    using IRMutator::visit;

    const string &prefix;

    void visit(const Variable *v) {
        if (v->param.defined()) {
            expr = v;
        } else {
            expr = Variable::make(v->type, prefix + v->name, v->reduction_domain);
        }
    }
    void visit(const Let *op) {
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        expr = Let::make(prefix + op->name, value, body);
    }
public:
    QualifyExpr(const string &p) : prefix(p) {}
};

Expr qualify(const string &prefix, Expr value) {
    QualifyExpr q(prefix);
    return q.mutate(value);
}

}
}
