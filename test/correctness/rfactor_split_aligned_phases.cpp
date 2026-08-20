#include "Halide.h"
#include <stdio.h>

// A variant of rfactor_split_aligned.cpp that preserves the *inner* (aligned,
// unrolled) half of the split via rfactor() instead of the outer half,
// turning it into four separate per-phase partial-sum accumulators that get
// combined at the end. rfactor() must still produce correct results here:
// this is precisely the "does rfactor tolerate splits with an alignment"
// question, exercised in the case where the aligned split is the one being
// preserved (and therefore promoted from an RVar with exact,
// compute_loop_bounds_after_split-derived bounds to an ordinary pure Var of
// the intermediate Func, whose bounds are instead re-derived by general
// bounds inference). That promotion means the compiler can no longer read
// off the new pure var's range directly from the split; it has to prove it
// symbolically from the surrounding min/max clamps instead, which is what
// the mux_count checks below are exercising.
//
// The second case additionally makes the RDom's own extent a runtime Param
// rather than a compile-time constant, so the split's "factor provably
// divides the extent" fast path (see apply_split in ApplySplit.cpp) can't
// fire either, and everything -- the boundary guard, the alignment, and the
// mux resolution -- has to come out of the general GuardWithIf path instead.

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

int expected_value(int r, int x, int off) {
    int selector = (4 + r - off) % 4;
    if (selector == 0) {
        return r + x;
    } else if (selector == 1) {
        return r * r + x;
    } else if (selector == 2) {
        return 2 * r + x;
    } else {
        return -r * (r + 1) + x;
    }
}

int test_fixed_extent() {
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
    Func intm = f.update(0).rfactor(ri, u);
    intm.compute_root();
    intm.update(0).unroll(u);

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
        Buffer<int> im = f.realize({10});
        for (int x = 0; x < 10; x++) {
            int expected = 0;
            for (int r = 0; r < 40; r++) {
                expected += expected_value(r, x, off);
            }
            if (im(x) != expected) {
                printf("im(%d) = %d instead of %d (offset: %d)\n", x, im(x), expected, off);
                return 1;
            }
        }
    }

    return 0;
}

int test_param_extent() {
    Var x{"x"};
    Func f{"f"};
    Param<int> extent{"extent"};
    RDom r(0, extent, "r");
    Param<int> offset{"offset"};
    offset.set_range(0, 3);

    f(x) = 0;
    f(x) += mux((r - offset) % 4, {r + x, r * r + x, 2 * r + x, -r * (r + 1) + x});

    RVar ro{"ro"}, ri{"ri"};
    f.update(0)
        .split(r, ro, ri, 4, offset, TailStrategy::GuardWithIf)
        .unroll(ri);

    Var u{"u"};
    Func intm = f.update(0).rfactor(ri, u);
    intm.compute_root();
    intm.update(0).unroll(u);

    Module module = f.compile_to_module({extent, offset});
    MuxCounter checker;
    for (const LoweredFunc &lf : module.functions()) {
        lf.body.accept(&checker);
    }
    if (checker.mux_count != 0) {
        printf("Expected 0 muxes (with a Param extent): %d\n", checker.mux_count);
        return 1;
    }

    // 40 is a multiple of the split factor; 37 is not, so it also exercises
    // the tail of the RDom's own range.
    for (int ext : {40, 37}) {
        for (int off = 0; off < 4; off++) {
            printf("Testing runtime extent %d, alignment %d\n", ext, off);
            extent.set(ext);
            offset.set(off);
            Buffer<int> im = f.realize({10});
            for (int x = 0; x < 10; x++) {
                int expected = 0;
                for (int r = 0; r < ext; r++) {
                    expected += expected_value(r, x, off);
                }
                if (im(x) != expected) {
                    printf("im(%d) = %d instead of %d (extent: %d, offset: %d)\n", x, im(x), expected, ext, off);
                    return 1;
                }
            }
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    if (test_fixed_extent()) {
        return 1;
    }
    if (test_param_extent()) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
