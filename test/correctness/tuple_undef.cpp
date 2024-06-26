#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

class CountStores : public IRVisitor {
public:
    int count;

    CountStores()
        : count(0) {
    }

protected:
    using IRVisitor::visit;

    void visit(const Store *op) override {
        count++;
    }
};

class CheckStoreCount : public IRMutator {
    int correct;

public:
    CheckStoreCount(int correct)
        : correct(correct) {
    }
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        CountStores c;
        s.accept(&c);

        if (c.count != correct) {
            printf("There were %d stores. There were supposed to be %d\n", c.count, correct);
            exit(1);
        }

        return s;
    }
};

int main(int argc, char **argv) {
    Buffer<int> a(1024, 1024), b(1024, 1024);
    const int A = (int)0xdeadbeef;
    const int B = (int)0xf00dcafe;

    printf("Test 1...\n");
    {
        Var x("x"), y("y");
        Func f("f");

        f(x, y) = Tuple(x + y, undef<int32_t>());
        f(x, y) = Tuple(f(x, y)[0] + undef<int32_t>(), f(x, y)[1] + 2);

        // There should be two stores: the undef stores should have been removed.
        f.add_custom_lowering_pass(new CheckStoreCount(2));

        // Pre-fill with unlikely values so we can verify that undef bits are untouched.
        a.fill(A);
        b.fill(B);
        f.realize({a, b});
        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = x + y;
                int correct_b = B + 2;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return 1;
                }
            }
        }
    }

    printf("Test 2...\n");
    {
        Var x("x"), y("y");
        Func f("f");

        f(x, y) = Tuple(x, y);
        f(x, y) = Tuple(undef<int>(), select(x < 20, 20 * f(x, y)[0], undef<int>()));

        // There should be three stores: the undef store to the 1st element of
        // the Tuple in the update definition should have been removed.
        f.add_custom_lowering_pass(new CheckStoreCount(3));

        a.fill(A);
        b.fill(B);
        f.realize({a, b});
        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = x;
                int correct_b = (x < 20) ? 20 * x : y;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return 1;
                }
            }
        }
    }

    printf("Test 3...\n");
    {
        Var x("x"), y("y");
        Func f("f"), g("g");

        f(x, y) = {0, 0};

        RDom r(0, 10);
        Expr arg_0 = clamp(select(r.x < 2, 13, undef<int>()), 0, 100);
        Expr arg_1 = clamp(select(r.x < 2, 23, undef<int>()), 0, 100);
        f(arg_0, arg_1) = {f(arg_0, arg_1)[0] + 10, f(arg_0, arg_1)[1] + 5};

        a.fill(A);
        b.fill(B);
        f.realize({a, b});
        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = (x == 13) && (y == 23) ? 20 : 0;
                int correct_b = (x == 13) && (y == 23) ? 10 : 0;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return 1;
                }
            }
        }
    }

    printf("Test 4...\n");
    {

        Var x("x"), y("y");
        Func f("f");

        f(x, y) = Tuple(undef<int32_t>(), undef<int32_t>());

        // There should be no stores since all Tuple values are undef.
        f.add_custom_lowering_pass(new CheckStoreCount(0));

        // Pre-fill with unlikely values so we can verify that undef bits are untouched.
        a.fill(A);
        b.fill(B);
        f.realize({a, b});
    }

    printf("Success!\n");
    return 0;
}
