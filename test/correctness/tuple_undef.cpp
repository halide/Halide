#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

class CountStores : public IRVisitor {
public:
    int count;

    CountStores() : count(0) {}

protected:
    using IRVisitor::visit;

    void visit(const Store *op) {
        count++;
    }
};

class CheckStoreCount : public IRMutator {
    int correct;
public:
    CheckStoreCount(int correct) : correct(correct) {}
    using IRMutator::mutate;

    Stmt mutate(Stmt s) {
        CountStores c;
        s.accept(&c);

        if (c.count != correct) {
            printf("There were %d stores. There were supposed to be %d\n", c.count, correct);
            exit(-1);
        }

        return s;
    }
};

int main(int argc, char **argv) {
    {
        Var x("x"), y("y");
        Func f("f");

        f(x, y) = Tuple(x + y, undef<int32_t>());
        f(x, y) += Tuple(undef<int32_t>(), Expr(2));

        // There should be two stores: the undef stores should have been removed.
        f.add_custom_lowering_pass(new CheckStoreCount(2));

        Realization result = f.realize(1024, 1024);
        Image<int> a = result[0], b = result[1];
        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = x + y;
                int correct_b = 2;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return -1;
                }
            }
        }
    }

    {
        Var x("x"), y("y");
        Func f("f");

        f(x, y) = Tuple(undef<int32_t>(), undef<int32_t>());

        // There should be no stores since all Tuple values are undef.
        f.add_custom_lowering_pass(new CheckStoreCount(0));

        Realization result = f.realize(1024, 1024);
    }

    printf("Success!\n");
    return 0;
}