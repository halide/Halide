#include "Halide.h"
#include <stdio.h>

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

    void visit(const For *op) override {
        IRVisitor::visit(op);
        for_count++;
    }

public:
    int for_count{0};
    int mux_count{0};
};

template<typename T>
T produce_mux_argument(int i, const T &x, const T &y) {
    return x * (i % 3) * 3 + y * (i % 3);
}

int test(bool gpu) {
    Var c{"c"};
    Var x{"x"}, xo{"xo"}, xi{"xi"};
    Var y{"y"}, yo{"yo"}, yi{"yi"};
    Var xio{"xio"}, xii{"xii"}, yio{"yio"}, yii{"yii"};
    Func f("f"), R("R"), G("G"), B("B");
    Param<int> offset_x{"offset_x"}, offset_y{"offset_y"};
    offset_x.set_range(0, 2);
    offset_y.set_range(0, 2);
    auto idx = [](const auto &x, const auto &y, const auto &offset_x, const auto &offset_y) {
        return (3 * ((y - offset_y) % 3)) + ((x - offset_x) % 3);
    };
    std::vector<Expr> ways;
    ways.reserve(9);
    for (int i = 0; i < 9; ++i) {
        ways.push_back(produce_mux_argument<Expr>(i, x, y));
    }
    R(x, y) = mux(idx(x, y, offset_x, offset_y), ways);
    G(x, y) = mux(idx(x, y, offset_x, offset_y), ways);
    B(x, y) = mux(idx(x, y, offset_x, offset_y), ways);
    f(x, y, c) = mux(c, {R(x, y), G(x, y), B(x, y)});
    f.output_buffer().dim(0).set_min(0);
    f.output_buffer().dim(1).set_min(0);

    if (gpu) {
        // A demosaicer-like GPU schedule: every block covers whole 3x3 tiles
        // of the pattern, every thread one 3x3 tile, and the channels are
        // staged per block in shared memory, three pixels at a time.
        const int tile = 12;
        f
            .aligned_split(x, xo, xi, tile, offset_x, Halide::TailStrategy::GuardWithIf)
            .aligned_split(y, yo, yi, tile, offset_y, Halide::TailStrategy::GuardWithIf)
            .reorder(c, xi, yi, xo, yo)
            .gpu_blocks(xo, yo)
            .split(xi, xio, xii, 3)
            .split(yi, yio, yii, 3)
            .gpu_threads(xio, yio)
            .unroll(xii)
            .unroll(yii)
            .bound(c, 0, 3)
            .unroll(c);

        for (Func *channel : {&R, &G, &B}) {
            channel->compute_at(f, xo)
                .bound_storage(x, tile)
                .bound_extent(x, tile)
                .align_bounds(x, 3, offset_x)
                .bound_storage(y, tile)
                .bound_extent(y, tile)
                .align_bounds(y, 3, offset_y)
                .gpu_threads(x, y)
                .vectorize(x, 3)
                .unroll(y, 3);
        }
    } else {
        // Split both dimensions so that the inner loops iterate over exactly one
        // 3x3 tile of the repeating pattern, anchored at (offset_x, offset_y).
        // Unrolling those inner loops should give each mux a constant index, so
        // every mux folds away to the single way it selects.
        f
            .aligned_split(x, xo, xi, 3, offset_x, Halide::TailStrategy::GuardWithIf)
            .aligned_split(y, yo, yi, 3, offset_y, Halide::TailStrategy::GuardWithIf)
            .never_partition_all()
            .reorder(c, xi, yi, xo, yo)
            .unroll(xi)
            .unroll(yi)
            .bound(c, 0, 3)
            .unroll(c);

        for (Func *channel : {&R, &G, &B}) {
            channel->compute_at(f, xo)
                .never_partition_all()
                // We want to compute a *whole* tile of the channel:
                .bound_storage(x, 3)
                .bound_extent(x, 3)
                .align_bounds(x, 3, offset_x)
                .bound_storage(y, 3)
                .bound_extent(y, 3)
                .align_bounds(y, 3, offset_y)
                // In principle, the .align_bounds() should be enough, but the simplifier in Halide
                // fails to determine that the producer tile perfectly overlaps with the consumer
                // tile. Both have extent 3, but the producer is not getting simplified. So here,
                // we can help it and tell it to use an extent of 3, after which the simplifier
                // manages to prove that 3 satisfies the minimum required. Proving the expression
                // is <= 3 is easier than proving it's == 3.
                // The simplifier gap manifests itself twice: once for the compute extent,
                // and once for the the storage extent. So we specify both. Unfortunately,
                // the scheduling order here is important: .bound_extent() must precede
                // .align_bounds() for the bounds inference to do the correct thing.
                //
                // Finally, we unroll to get rid of the muxes:
                .unroll(x)
                .unroll(y);
        }
    }

    // The GPU kernels are compiled to device code by the time we get a
    // Module, so only the CPU schedule is inspected for loops and muxes.
    MuxCounter checker;
    if (!gpu) {
        Module module = f.compile_to_module({offset_x, offset_y});
        for (const LoweredFunc &lf : module.functions()) {
            lf.body.accept(&checker);
        }
    }

    const int W = 32, H = 32;
    for (int oy = 0; oy < 3; oy++) {
        for (int ox = 0; ox < 3; ox++) {
            printf("Testing runtime alignment: x=%d y=%d\n", ox, oy);
            offset_x.set(ox);
            offset_y.set(oy);
            Buffer<int> im = f.realize({W, H, 3});
            im.copy_to_host();

            for (int cc = 0; cc < 3; cc++) {
                for (int y = 0; y < H; y++) {
                    for (int x = 0; x < W; x++) {
                        // Bias by 3 so the operands of % stay non-negative,
                        // where C++'s truncated % agrees with Halide's
                        // Euclidean %.
                        int selector = idx(3 + x, 3 + y, ox, oy);
                        int expected = produce_mux_argument(selector, x, y);
                        if (im(x, y, cc) != expected) {
                            printf("im(%d, %d, %d) = %d instead of %d (selector: %d)\n",
                                   x, y, cc, im(x, y, cc), expected, selector);
                            return 1;
                        }
                    }
                }
            }
        }
    }

    if (gpu) {
        return 0;
    }
    if (checker.mux_count != 0) {
        printf("Expected 0 muxes, got: %d\n", checker.mux_count);
        return 1;
    }
    if (checker.for_count != 2) {
        printf("Expected 2 for loops, got: %d\n", checker.for_count);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (test(false) != 0) {
        return 1;
    }
    if (get_jit_target_from_environment().has_gpu_feature()) {
        printf("Testing the GPU schedule\n");
        if (test(true) != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
