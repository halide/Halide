#include "Lambda.h"
#include "IR.h"
#include "IRVisitor.h"

namespace Halide {
namespace {
class CountImplicitVars : public Internal::IRGraphVisitor {
public:
    int count;

    CountImplicitVars(const Expr &e)
        : count(0) {
        e.accept(this);
    }

    using IRGraphVisitor::visit;

    void visit(const Internal::Variable *v) override {
        int index = Var::implicit_index(v->name);
        if (index != -1) {
            if (index >= count) count = index + 1;
        }
    }
};
}  // namespace

Func lambda(Expr e) {
    Func f("lambda" + Internal::unique_name('_'));
    CountImplicitVars implicit_count(e);
    if (implicit_count.count > 0) {
        f(_) = e;
    } else {
        f() = e;
    }
    return f;
}
}  // namespace Halide
