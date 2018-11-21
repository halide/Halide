#include "Qualify.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::string;

// Prefix all names in an expression with some string.
class QualifyExpr : public IRMutator2 {
    using IRMutator2::visit;

    const string &prefix;

    Expr visit(const Variable *v) override {
        if (v->param.defined()) {
            return v;
        } else {
            return Variable::make(v->type, prefix + v->name, v->reduction_domain);
        }
    }
    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        return Let::make(prefix + op->name, value, body);
    }
public:
    QualifyExpr(const string &p) : prefix(p) {}
};

Expr qualify(const string &prefix, Expr value) {
    QualifyExpr q(prefix);
    return q.mutate(value);
}

}  // namespace Internal
}  // namespace Halide
