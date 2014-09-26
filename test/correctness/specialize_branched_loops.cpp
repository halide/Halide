#include <Halide.h>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

class ContainsBranches : public IRVisitor {
public:
    bool result;
    ContainsBranches() : result(false) {}

    using IRVisitor::visit;

    void visit(const Select *op) {
        result = true;
        IRVisitor::visit(op);
    }

    void visit(const IfThenElse *op) {
        result = true;
        IRVisitor::visit(op);
    }
};

bool uses_branches(Func f) {
    Target t = get_jit_target_from_environment();
    t.set_feature(Target::NoBoundsQuery);
    t.set_feature(Target::NoAsserts);
    Stmt s = Internal::lower(f.function(), t);
    ContainsBranches b;
    s.accept(&b);
    return b.result;
}


int main(int argc, char **argv) {

    Func f;
    Var x, y, c;
    f(x, y, c) = 1 + select(c < 1, x,
                            c == 1, y,
                            x + y);
    f.vectorize(x, 4);

    // The select in c should go away.
    if (uses_branches(f)) {
        printf("There weren't supposed to be branches!\n");
        return -1;
    }

    Image<int> result = f.realize(10, 10, 3);
    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            for (int c = 0; c < result.channels(); c++) {
                int correct = 1 + (c == 0 ? x : (c == 1 ? y : x + y));
                if (result(x, y, c) != correct) {
                    printf("result(%d, %d, %d) = %d instead of %d\n",
                           x, y, c, result(x, y, c), correct);
                    return -1;
                }
            }
        }
    }

}

