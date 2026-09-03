#include "Halide.h"
#include <stdio.h>

// A minimal distillation of a demosaicer scheduled with aligned splits. A
// demosaicer selects which interpolation to apply from the position within the
// sensor's colour filter pattern -- a mux over (x - offset) % period, where the
// period is 2 for a Bayer sensor and 6 for an X-Trans one -- and is scheduled
// with a split aligned to that same offset precisely so that the phase is
// constant across every tile, letting the mux resolve at compile time.
//
// For that to happen the compiler has to see two things. The alignment the
// split adds into the reconstructed loop variable has to cancel against the
// subtraction at the use site, even though the split binds that variable to a
// LetStmt and the offset is a runtime value (see reduce_expr_modulo_symbolic).
// And the vectorized loop has to be deinterleaved by the period, so that each
// of the resulting slices has a phase of its own (see Deinterleave.cpp).

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

int test(int period) {
    const int width = 1024;
    // The tile has to be a whole number of periods for the phase to be the
    // same in every tile, and the vector a whole number of periods for each
    // deinterleaved slice to have a single phase.
    const int tile = period * 64;
    const int vec = period * 2;

    Var x{"x"}, xo{"xo"}, xi{"xi"};
    Param<int> off{"off"};
    off.set_range(0, period - 1);

    // One phase per position in the pattern.
    std::vector<Expr> phases;
    for (int i = 0; i < period; i++) {
        phases.push_back(x * (i + 1) + i);
    }

    Func prod{"prod"};
    prod(x) = mux((x - off) % period, phases);

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
        .align_bounds(x, period, off)
        .vectorize(x, vec, TailStrategy::RoundUp);

    Pipeline p(out);

    // Every phase is known at compile time, so no mux should survive lowering.
    Module m = p.compile_to_module({off}, "split_aligned_mux_phase");
    MuxCounter checker;
    m.functions().front().body.accept(&checker);
    if (checker.mux_count != 0) {
        printf("Period %d: expected 0 muxes, got %d\n", period, checker.mux_count);
        return 1;
    }

    // And it still has to compute the right thing.
    for (int o = 0; o < period; o++) {
        off.set(o);
        Buffer<int> result = p.realize({width});
        for (int i = 0; i < width; i++) {
            int phase = (((i - o) % period) + period) % period;
            int correct = i * (phase + 1) + phase;
            if (result(i) != correct) {
                printf("Period %d, off = %d: result(%d) = %d instead of %d\n",
                       period, o, i, result(i), correct);
                return 1;
            }
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    // Bayer, a three-phase pattern, and X-Trans.
    for (int period : {2, 3, 6}) {
        if (test(period) != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
