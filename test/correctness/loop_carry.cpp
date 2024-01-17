#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

// Wrapper class to call loop_carry on a given statement.
class LoopCarryWrapper : public IRMutator {
    using IRMutator::visit;

    int register_count_;
    Stmt mutate(const Stmt &stmt) override {
        return simplify(loop_carry(stmt, register_count_));
    }

public:
    LoopCarryWrapper(int register_count)
        : register_count_(register_count) {
    }
};

int main(int argc, char **argv) {
    Func input;
    Func g;
    Func h;
    Func f;
    Var x, y, xo, yo, xi, yi;

    input(x, y) = x + y;

    Expr sum_expr = 0;
    for (int ix = -100; ix <= 100; ix++) {
        // Generate two chains of sums, but only one of them will be carried.
        sum_expr += input(x, y + ix);
        sum_expr += input(x + 13, y + 2 * ix);
    }
    g(x, y) = sum_expr;
    h(x, y) = g(x, y) + 12;
    f(x, y) = h(x, y);

    // Make a maximum number of the carried values very large for the purpose
    // of this test.
    constexpr int kMaxRegisterCount = 1024;
    f.add_custom_lowering_pass(new LoopCarryWrapper(kMaxRegisterCount));

    const int size = 128;
    f.compute_root()
        .bound(x, 0, size)
        .bound(y, 0, size);

    h.compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

    g.compute_at(h, xo)
        .reorder(y, x)
        .vectorize(x, 4);

    input.compute_root();

    f.realize({size, size});

    printf("Success!\n");
    return 0;
}
