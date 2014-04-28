#include "OneToOne.h"
#include "Derivative.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "IROperator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

class FindVariable : public IRGraphVisitor {
public:
    const Variable *var;
    bool multiple;
    Scope<int> internal;

    FindVariable() : var(NULL), multiple(false) {}

private:
    using IRGraphVisitor::visit;

    void visit(const Let *let) {
        include(let->value);
        internal.push(let->name, 0);
        include(let->body);
        internal.pop(let->name);
    }

    void visit(const Variable *v) {
        if (internal.contains(v->name)) {
            // Skip things defined by let exprs.
        } else if (v->param.defined()) {
            // Skip parameters. They are constants for our purposes.
        } else if (!var || v == var || v->name == var->name) {
            var = v;
        } else {
            multiple = true;
        }
    }
};

bool is_one_to_one(Expr e) {
    internal_assert(e.type() == Int(32))
        << "is_one_to_one only works on expressions of type Int(32)\n";

    // First find the variable
    FindVariable finder;
    e.accept(&finder);
    if (finder.multiple) {
        // More than one variable present.
        return false;
    }
    if (finder.var == NULL) {
        // No variables present.
        return false;
    }

    Expr d = finite_difference(e, finder.var->name);
    if (!d.defined()) {
        // Taking the finite difference failed.
        return false;
    }

    Expr strictly_positive = simplify(d > 0);
    Expr strictly_negative = simplify(d < 0);

    return (is_one(strictly_positive) ||
            is_one(strictly_negative));
}

namespace {
void check(Expr e, bool result) {
    if (is_one_to_one(e) != result) {
        internal_error << "Failure testing is_one_to_one:\n"
                       << e << " should have returned "
                       << result << "\n";
    }
}
}

void is_one_to_one_test() {
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    check(3*x-2, true);
    check(Let::make("y", 6*x, x + y), true);
    check(x/7, false);
    check(cast<int>(cos(x)), false);
    check(x - x, false);
    check(-37 * x + 36 * x, true);
    check(x + y, false);
}

}
}
