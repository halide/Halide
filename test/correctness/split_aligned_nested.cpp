#include "Halide.h"
#include <stdio.h>

// Nests two aligned splits: x is split into (xo, xi) aligned to p1, and then
// the resulting outer var xo is itself split into (xoo, xoi) aligned to a
// second, independent runtime Param p2. This exercises the aligned-split
// machinery (ApplySplit.cpp's apply_split/compute_loop_bounds_after_split)
// on a var whose own loop_min is not a compile-time constant (it comes from
// the first split's outer bound, which is a function of p1), stacked with a
// second, unrelated alignment. The mux selector only depends on p1, so this
// is primarily a correctness test of composing aligned splits -- the
// reconstruction of x from xoo, xoi, and xi has to be correct for every
// combination of the two independently-varying runtime alignments.

using namespace Halide;
using namespace Halide::Internal;

class MuxCounter : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        if (op->is_intrinsic(Call::IntrinsicOp::mux)) {
            mux_count++;
        }
    }

public:
    int mux_count{0};
};

int main(int argc, char **argv) {
    Var x{"x"}, xo{"xo"}, xi{"xi"}, xoo{"xoo"}, xoi{"xoi"};
    Func f{"f"};
    Param<int> p1{"p1"}, p2{"p2"};
    p1.set_range(0, 3);
    p2.set_range(0, 2);

    f(x) = mux((x - p1) % 4, {x, x * x, 2 * x, -x * (x + 1)});
    f.output_buffer().dim(0).set_min(0);

    f.split(x, xo, xi, 4, p1, TailStrategy::GuardWithIf)
        .split(xo, xoo, xoi, 3, p2, TailStrategy::GuardWithIf)
        .unroll(xi);

    Module module = f.compile_to_module({p1, p2});
    MuxCounter checker;
    for (const LoweredFunc &lf : module.functions()) {
        lf.body.accept(&checker);
    }
    if (checker.mux_count != 0) {
        printf("Expected 0 muxes: %d\n", checker.mux_count);
        return 1;
    }

    for (int a1 = 0; a1 < 4; a1++) {
        for (int a2 = 0; a2 < 3; a2++) {
            printf("Testing runtime alignment: p1=%d p2=%d\n", a1, a2);
            p1.set(a1);
            p2.set(a2);
            Buffer<int> im = f.realize({61});
            for (int x = 0; x < 61; x++) {
                int selector = (4 + x - a1) % 4;
                int expected;
                if (selector == 0) {
                    expected = x;
                } else if (selector == 1) {
                    expected = x * x;
                } else if (selector == 2) {
                    expected = 2 * x;
                } else {
                    expected = -x * (x + 1);
                }
                if (im(x) != expected) {
                    printf("im(%d) = %d instead of %d (p1: %d, p2: %d)\n", x, im(x), expected, a1, a2);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
