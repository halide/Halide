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

int main(int argc, char **argv) {
    Var c{"c"};
    Var x{"x"}, xo{"xo"}, xi{"xi"};
    Var y{"y"}, yo{"yo"}, yi{"yi"};
    Func f("f"), R("R"), G("G"), B("B");
    Param<int> offset_x{"offset_x"}, offset_y{"offset_y"};
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

    // Split both dimensions so that the inner loops iterate over exactly one
    // 3x3 tile of the repeating pattern, anchored at (offset_x, offset_y).
    // Unrolling those inner loops should give each mux a constant index, so
    // every mux folds away to the single way it selects.
    f
        .split(x, xo, xi, 3, offset_x, Halide::TailStrategy::GuardWithIf)
        .split(y, yo, yi, 3, offset_y, Halide::TailStrategy::GuardWithIf)
        .never_partition_all()
        .reorder(c, xi, yi, xo, yo)
        .unroll(xi)
        .unroll(yi)
        .bound(c, 0, 3)
        .unroll(c);

    for (Func *channel : {&R, &G, &B}) {
        channel->compute_at(f, xo).unroll(x).unroll(y).never_partition_all();
    }

    Module module = f.compile_to_module({offset_x, offset_y});
    MuxCounter checker;
    for (const LoweredFunc &lf : module.functions()) {
        lf.body.accept(&checker);
    }

    const int W = 32, H = 32;
    for (int oy = 0; oy < 3; oy++) {
        for (int ox = 0; ox < 3; ox++) {
            printf("Testing runtime alignment: x=%d y=%d\n", ox, oy);
            offset_x.set(ox);
            offset_y.set(oy);
            Buffer<int> im = f.realize({W, H, 3});

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

    if (checker.mux_count != 0) {
        printf("Expected 0 muxes, got: %d\n", checker.mux_count);
        return 1;
    }
    if (checker.for_count != 2) {
        printf("Expected 2 for loops, got: %d\n", checker.for_count);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
