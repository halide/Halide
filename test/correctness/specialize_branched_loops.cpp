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

    Image<int> f_result = f.realize(10, 10, 3);
    for (int y = 0; y < f_result.height(); y++) {
        for (int x = 0; x < f_result.width(); x++) {
            for (int c = 0; c < f_result.channels(); c++) {
                int correct = 1 + (c == 0 ? x : (c == 1 ? y : x + y));
                if (f_result(x, y, c) != correct) {
                    printf("f_result(%d, %d, %d) = %d instead of %d\n",
                           x, y, c, f_result(x, y, c), correct);
                    return -1;
                }
            }
        }
    }


    Func g;
    g(x, y, c) = select(c > 1, 2*x,
                        c == 1, x - y,
                        y)
            + select(c < 1, x,
                     c == 1, y,
                     x + y);
    g.vectorize(x, 4);

    g.output_buffer()
            .set_min(0,0).set_min(1,0).set_min(2,0)
            .set_extent(0,10).set_extent(1,10).set_extent(2,3);

    // The select in c should go away.
    if (uses_branches(g)) {
        printf("There weren't supposed to be branches!\n");
        return -1;
    }

    Image<int> g_result = g.realize(10, 10, 3);
    for (int y = 0; y < g_result.height(); y++) {
        for (int x = 0; x < g_result.width(); x++) {
            for (int c = 0; c < g_result.channels(); c++) {
                int correct = (c > 1 ? 2*x : (c == 1 ? x - y : y))
                        + (c < 1 ? x : (c == 1 ? y : x + y));
                if (g_result(x, y, c) != correct) {
                    printf("g_result(%d, %d, %d) = %d instead of %d\n",
                           x, y, c, g_result(x, y, c), correct);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
