#include "Halide.h"
#include <stdio.h>

// A minimal distillation of a bilinear demosaicer scheduled with aligned
// splits. A demosaicer selects which interpolation to apply from the position
// within the Bayer pattern -- a mux over (x - bayer_offset) % 2 -- and is
// scheduled with a split aligned to that same offset precisely so that the
// phase is constant across every tile, letting the mux resolve at compile
// time.
//
// The alignment cancels only if the simplifier can see the "+ off" that the
// aligned split puts into the reconstructed loop variable at the same time as
// the "- off" at the use site. When the producer is computed inside the
// consumer's tile and then split again itself (here to vectorize), the second
// split binds the reconstructed variable to a LetStmt, and the simplifier
// cannot see through a LetStmt. The "+ off" is then hidden behind an opaque
// name, (x - off) % 2 never reduces to a function of the inner loop variable
// alone, and the mux survives to the end of lowering.

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
    const int width = 1024;
    const int tile = 128;
    const int vec = 8;

    Var x{"x"}, xo{"xo"}, xi{"xi"};
    Param<int> off{"off"};
    off.set_range(0, 1);

    // Two "phases", selected by the position relative to the offset.
    Func prod{"prod"};
    prod(x) = mux((x - off) % 2, {x + 1, x * 2});

    Func out{"out"};
    out(x) = prod(x);

    out.output_buffer().dim(0).set_min(0);

    // The consumer is tiled with a split aligned to the same offset, so every
    // tile starts at a position congruent to off.
    out.split(x, xo, xi, tile, off, TailStrategy::GuardWithIf)
        .vectorize(xi)
        .never_partition_all();

    // The producer is computed per tile. align_bounds keeps its min congruent
    // to off as well, so its phase really is constant, and then it is split
    // again to vectorize.
    prod.compute_at(out, xo)
        .never_partition_all()
        .align_bounds(x, 2, off)
        .vectorize(x, vec, TailStrategy::RoundUp);

    Pipeline p(out);

    // Every phase is known at compile time, so no mux should survive lowering.
    Module m = p.compile_to_module({off}, "split_aligned_mux_phase");
    MuxCounter checker;
    m.functions().front().body.accept(&checker);
    if (checker.mux_count != 0) {
        printf("Expected 0 muxes, got %d\n", checker.mux_count);
        return 1;
    }

    // And it still has to compute the right thing.
    for (int o = 0; o <= 1; o++) {
        off.set(o);
        Buffer<int> result = p.realize({width});
        for (int i = 0; i < width; i++) {
            int correct = (((i - o) % 2) + 2) % 2 == 0 ? i + 1 : i * 2;
            if (result(i) != correct) {
                printf("off = %d: result(%d) = %d instead of %d\n",
                       o, i, result(i), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
