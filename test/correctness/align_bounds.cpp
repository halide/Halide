#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class CheckForSelects : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Select *op) {
        result = true;
    }
public:
    bool result = false;
};


int main(int argc, char **argv) {
    Func f, g, h;
    Var x;

    f(x) = 3;

    g(x) = select(x % 2 == 0, f(x+1), f(x-1)+8);

    Param<int> p;
    h(x) = g(x-p) + g(x+p);

    f.compute_root();
    g.compute_root().align_bounds(x, 2).unroll(x, 2);

    // The lowered IR should contain no selects.
    Module m = g.compile_to_module({p});
    CheckForSelects checker;
    m.functions()[0].body.accept(&checker);
    if (checker.result) {
        printf("Lowered code contained a select\n");
        return -1;
    }

    p.set(3);
    Image<int> result = h.realize(10);

    for (int i = 0; i < 10; i++) {
        int correct = (i&1) == 1 ? 6 : 22;
        if (result(i) != correct) {
            printf("result(%d) = %d instead of %d\n",
                   i, result(i), correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
