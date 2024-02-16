#include "Qualify.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::string;

namespace {

// Prefix all names in an expression with some string.
class QualifyExpr : public IRMutator {
    using IRMutator::visit;

    std::string_view prefix;

    Expr visit(const Variable *v) override {
        if (v->param.defined()) {
            return v;
        } else {
            return Variable::make(v->type, concat(prefix, v->name), v->reduction_domain);
        }
    }
    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        return Let::make(concat(prefix, op->name), value, body);
    }

public:
    QualifyExpr(std::string_view p)
        : prefix(p) {
    }
};

}  // namespace

Expr qualify(std::string_view prefix, const Expr &value) {
    QualifyExpr q(prefix);
    return q.mutate(value);
}

}  // namespace Internal
}  // namespace Halide
