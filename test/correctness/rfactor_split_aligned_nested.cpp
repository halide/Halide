#include "Halide.h"
#include <stdio.h>

// A companion to rfactor_split_aligned.cpp and split_aligned_nested.cpp:
// after r's aligned split (factor 4, aligned to offset) is rfactored on its
// outer half into a preserved pure var u, u is itself split again with a
// second, independent alignment (p2), tried with GuardWithIf,
// RoundUpAndBlend, and ShiftInwardsAndBlend.
//
// GuardWithIf and Predicate are the only tail strategies Stage::split allows
// on an RVar (splitting r itself), because RoundUp/ShiftInwards-family
// strategies would change the meaning of a reduction by recomputing or
// overrunning it -- but u is an ordinary pure Var of the intermediate
// Func's own update definition, so RoundUpAndBlend/ShiftInwardsAndBlend
// (the update-definition-safe counterparts of RoundUp/ShiftInwards) are
// legal there, and are exactly the tail strategies meant for vectorizing
// an update like this one.
//
// This combination exercises boundary handling in ApplySplit.cpp
// (apply_split's ShiftInwardsAndBlend/RoundUpAndBlend branches) that plain,
// unnested aligned splits don't: u's own old_min is not a compile-time
// constant (it comes from r's split, a function of the runtime offset
// Param), so both the low and high boundary tiles of u's split can only be
// distinguished from the interior at runtime.

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
    for (auto ts : {TailStrategy::GuardWithIf, TailStrategy::RoundUpAndBlend, TailStrategy::ShiftInwardsAndBlend}) {
        printf("Testing tail strategy: %d\n", (int)ts);

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

        Var u{"u"}, uo{"uo"}, ui{"ui"};
        Param<int> p2{"p2"};
        p2.set_range(0, 1);

        Func intm = f.update(0).rfactor(ro, u);
        intm.compute_root();
        intm.update(0)
            .split(u, uo, ui, 2, p2, ts)
            .vectorize(ui);

        Module module = f.compile_to_module({offset, p2});
        MuxCounter checker;
        for (const LoweredFunc &lf : module.functions()) {
            lf.body.accept(&checker);
        }
        if (checker.mux_count != 0) {
            printf("Expected 0 muxes: %d\n", checker.mux_count);
            return 1;
        }

        for (int off = 0; off < 4; off++) {
            for (int a2 = 0; a2 < 2; a2++) {
                offset.set(off);
                p2.set(a2);
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
                        printf("im(%d) = %d instead of %d (offset: %d, p2: %d, ts: %d)\n",
                               x, im(x), expected, off, a2, (int)ts);
                        return 1;
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
