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
    return x * (i % 6) * 6 + y * (i % 6);
}

int main(int argc, char **argv) {
    Var c{"c"};
    Var x{"x"}, xo{"xo"}, xi{"xi"};
    Var y{"y"}, yo{"yo"}, yi{"yi"};
    Func f("f"), R("R"), G("G"), B("B");
    Param<int> offset_x{"offset_x"}, offset_y{"offset_y"};
    // offset_x.set_range(0, 5);
    // offset_y.set_range(0, 5);
    auto idx = [](const auto &x, const auto &y, const auto &offset_x, const auto &offset_y) {
        return (6 * ((y - offset_y) % 6)) + ((x - offset_x) % 6);
    };
    std::vector<Expr> ways;
    ways.reserve(36);
    for (int i = 0; i < 36; ++i) {
        ways.push_back(produce_mux_argument<Expr>(i, x, y));
    }
    R(x, y) = mux(idx(x, y, offset_x, offset_y), ways);
    G(x, y) = mux(idx(x, y, offset_x, offset_y), ways);
    B(x, y) = mux(idx(x, y, offset_x, offset_y), ways);
    f(x, y, c) = mux(c, {R(x, y), G(x, y), B(x, y)});
    f.output_buffer().dim(0).set_min(0);
    f.output_buffer().dim(1).set_min(0);

    f
        .split(x, xo, xi, 6, offset_x, Halide::TailStrategy::GuardWithIf)
        .split(y, yo, yi, 6, offset_y, Halide::TailStrategy::GuardWithIf)
        .never_partition_all()
        .reorder(xi, yi, xo, yo)
        .unroll(xi)
        .unroll(yi)
        .bound(c, 0, 3)
        .unroll(c);

    for (Func *channel : {&R, &G, &B}) {
        channel->compute_at(f, xo).unroll(x).unroll(y).never_partition_all();
    }

    Module module = f.compile_to_module({offset_x, offset_y});
    MuxCounter checker;
    for (const LoweredFunc &f : module.functions()) {
        f.body.accept(&checker);
    }

    for (int i = 0; i < 4; i++) {
        printf("Testing runtime alignment: x=%d y=%d\n", i / 6, i % 6);
        offset_x.set(i / 6);
        offset_y.set(i % 6);
        Buffer<int> im = f.realize({32, 32, 3});
        f.realize(im, get_target_from_environment());

        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                int selector = idx(6 + x, 6 + y, offset_x.get(), offset_y.get());
                int expected = produce_mux_argument(selector, x, y);
                if (im(x, y) != expected) {
                    printf("im(%d, %d) = %d instead of %d (selector: %d)\n", x, y, im(x, y), expected, selector);
                    return 1;
                }
            }
        }
    }

    if (checker.mux_count != 0) {
        std::printf("Expected 0 muxes: %d\n", checker.mux_count);
        return 1;
    }
    if (checker.for_count != 1) {
        std::printf("Expected 3 for loops: %d\n", checker.for_count);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
