#include "expr_util.h"

#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class FindVars : public IRVisitor {
    Scope<> lets;

    void visit(const Variable *op) override {
        if (!lets.contains(op->name)) {
            vars[op->name]++;
        }
    }

    void visit(const Let *op) override {
        op->value.accept(this);
        {
            ScopedBinding<> bind(lets, op->name);
            op->body.accept(this);
        }
    }
public:
    std::map<std::string, int> vars;
};

std::map<std::string, int> find_vars(const Expr &e) {
    FindVars f;
    e.accept(&f);
    return f.vars;
}
