#include "Halide.h"
#include <stdio.h>

// A 2D companion to rfactor_split_aligned.cpp. Here rfactor() is applied to
// an RVar (r.x) that is unrelated to the one carrying the aligned split
// (r.y), which is the more common pattern in practice: factor out one
// reduction dimension for parallel/vector reduction while a separate
// dimension is scheduled with an alignment-aware split so a
// runtime-offset-dependent mux() can be resolved statically once its half of
// the split is unrolled. Since r.y's split is entirely unrelated to the
// preserved var, both halves of the split remain ordinary (non-preserved)
// reduction variables of the intermediate Func, retaining their exact
// compile-time loop bounds and so still collapsing the mux to nothing.

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
    Var x{"x"}, y{"y"};
    Func f{"f"};
    RDom r(0, 20, 0, 16, "r");
    Param<int> offset{"offset"};
    offset.set_range(0, 3);

    f(x, y) = 0;
    f(x, y) += mux((r.y - offset) % 4,
                   {r.x + r.y + x + y,
                    r.x * r.y + x - y,
                    2 * r.x - r.y + x,
                    -r.x * (r.y + 1) + y}) *
               select(r.x % 2 == 0, 1, -1);

    RVar ryo{"ryo"}, ryi{"ryi"};
    f.update(0)
        .split(r.y, ryo, ryi, 4, offset, TailStrategy::GuardWithIf)
        .unroll(ryi);

    Var u{"u"};
    Func intm = f.update(0).rfactor(r.x, u);
    intm.compute_root();
    intm.update(0).parallel(u);

    Module module = f.compile_to_module({offset});
    MuxCounter checker;
    for (const LoweredFunc &lf : module.functions()) {
        lf.body.accept(&checker);
    }
    if (checker.mux_count != 0) {
        printf("Expected 0 muxes: %d\n", checker.mux_count);
        return 1;
    }

    for (int off = 0; off < 4; off++) {
        printf("Testing runtime alignment: %d\n", off);
        offset.set(off);
        Buffer<int> im = f.realize({6, 6});
        for (int y = 0; y < 6; y++) {
            for (int x = 0; x < 6; x++) {
                int expected = 0;
                for (int rx = 0; rx < 20; rx++) {
                    for (int ry = 0; ry < 16; ry++) {
                        int selector = (4 + ry - off) % 4;
                        int term;
                        if (selector == 0) {
                            term = rx + ry + x + y;
                        } else if (selector == 1) {
                            term = rx * ry + x - y;
                        } else if (selector == 2) {
                            term = 2 * rx - ry + x;
                        } else {
                            term = -rx * (ry + 1) + y;
                        }
                        term *= (rx % 2 == 0) ? 1 : -1;
                        expected += term;
                    }
                }
                if (im(x, y) != expected) {
                    printf("im(%d, %d) = %d instead of %d (offset: %d)\n", x, y, im(x, y), expected, off);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
