#include "Halide.h"
#include <stdio.h>

// rfactor() eagerly applies any splits present on the RVar(s) it's given (see
// Stage::rfactor / project_rdom in Func.cpp), so it needs to tolerate splits
// that carry an alignment (Stage::split's 'align' argument) just as well as
// ordinary ones. This test factors the *outer* half of an aligned split of
// the reduction variable out into a parallel-reducible intermediate Func,
// while unrolling the *inner* (aligned) half in the reducing computation.
// Because the inner half is not itself preserved by rfactor(), it keeps the
// exact loop bounds computed by compute_loop_bounds_after_split (rather than
// being re-derived by general bounds inference), so unrolling it still lets
// the compiler resolve the runtime-offset mux() to a compile-time constant
// per lane, exactly as it does without rfactor in split_aligned.cpp.

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
    Var x{"x"};
    Func f{"f"};
    RDom r(0, 40, "r");
    Param<int> offset{"offset"};
    offset.set_range(0, 3);

    f(x) = 0;
    f(x) += mux((r - offset) % 4, {r + x, r * r + x, 2 * r + x, -r * (r + 1) + x});

    RVar ro{"ro"}, ri{"ri"};
    f.update(0)
        .split(r, ro, ri, 4, offset, TailStrategy::GuardWithIf)
        .unroll(ri);

    Var u{"u"};
    Func intm = f.update(0).rfactor(ro, u);
    intm.compute_root();
    intm.update(0).parallel(u);

    Module module = f.compile_to_module({offset});
    MuxCounter checker;
    for (const LoweredFunc &lf : module.functions()) {
        lf.body.accept(&checker);
    }
    if (checker.mux_count != 0) {
        printf("Expected 0 muxes (the aligned+unrolled inner split var should "
               "resolve the mux at compile time even after rfactor): %d\n",
               checker.mux_count);
        return 1;
    }

    for (int off = 0; off < 4; off++) {
        printf("Testing runtime alignment: %d\n", off);
        offset.set(off);
        Buffer<int> im = f.realize({10});
        for (int x = 0; x < 10; x++) {
            int expected = 0;
            for (int r = 0; r < 40; r++) {
                int selector = (4 + r - off) % 4;
                int term;
                if (selector == 0) {
                    term = r + x;
                } else if (selector == 1) {
                    term = r * r + x;
                } else if (selector == 2) {
                    term = 2 * r + x;
                } else {
                    term = -r * (r + 1) + x;
                }
                expected += term;
            }
            if (im(x) != expected) {
                printf("im(%d) = %d instead of %d (offset: %d)\n", x, im(x), expected, off);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
