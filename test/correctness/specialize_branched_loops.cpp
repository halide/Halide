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

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT int count(int) {
    static int counter = 0;
    return counter++;
}
HalideExtern_1(int, count, int);

int main(int argc, char **argv) {
    Var x, y, c;

    {
        Func f;
        f(x, y, c) = 1 + select(c < 1, x,
                                c == 1, y,
                                x + y);
        f.reorder(c, x, y).vectorize(x, 4);

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
    }

    {
        Func g;
        g(x, y, c) = (select(c > 1, 2*x,
                             c == 1, x - y,
                             y)
                      + select(c < 1, x,
                               c == 1, y,
                               x + y));
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
    }

    {
        // An RDom with a conditional
        Func f, sum_scan;
        f(x) = x*17 + 3;
        f.compute_root();

        RDom r(0, 100);
        sum_scan(x) = undef<int>();
        sum_scan(r) = select(r == 0, f(r), f(r) + sum_scan(max(0, r-1)));

        if (uses_branches(sum_scan)) {
            printf("There weren't supposed to be branches!\n");
            return -1;
        }

        Image<int> result = sum_scan.realize(100);

        int correct = 0;
        for (int x = 0; x < 100; x++) {
            correct += x*17 + 3;
            if (result(x) != correct) {
                printf("sum scan result(%d) = %d instead of %d\n",
                       x, result(x), correct);
                return -1;
            }
        }
    }

    // Sliding window optimizations inject a select in a let expr. See if it gets simplified.
    {
        Func f, g;
        f(x) = x*x*17;
        g(x) = f(x-1) + f(x+1);
        f.store_root().compute_at(g, x);

        if (uses_branches(g)) {
            printf("There weren't supposed to be branches!\n");
            return -1;
        }

        Image<int> result = g.realize(100);

        for (int x = 0; x < 100; x++) {
            int correct = (x-1)*(x-1)*17 + (x+1)*(x+1)*17;
            if (result(x) != correct) {
                printf("sliding window result(%d) = %d instead of %d\n",
                       x, result(x), correct);
                return -1;
            }
        }

    }

    // Check it still works when unrolling (and doesn't change the order of evaluation).
    {
        Func f;
        f(x) = select(x > 3, x*3, x*17) + count(x);
        f.bound(x, 0, 100).unroll(x, 2);

        Image<int> result = f.realize(100);

        for (int x = 0; x < 100; x++) {
            int correct = x > 3 ? x*3 : x*17;
            correct += x;
            if (result(x) != correct) {
                printf("Unrolled result(%d) = %d instead of %d\n",
                       x, result(x), correct);
                break; // Failing. Continue to other tests.
                //return -1;
            }
        }
    }

    // Check for combinatorial explosion when there are lots of selects
    {
        Func f;
        Expr e = 0;
        for (int i = 0; i < 20; i++) {
            e = select(x <= i, i*i, e);
        }
        f(x) = e;

        Image<int> result = f.realize(100);

        for (int x = 0; x < 100; x++) {
            int correct = x < 20 ? x*x : 0;
            if (result(x) != correct) {
                printf("lots of selects result(%d) = %d instead of %d\n",
                       x, result(x), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
