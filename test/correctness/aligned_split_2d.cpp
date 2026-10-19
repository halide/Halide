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

int test(bool gpu) {
    Var x{"x"}, xo{"xo"}, xi{"xi"}, xoo{"xoo"}, xoi{"xoi"};
    Var y{"y"}, yo{"yo"}, yi{"yi"}, yoo{"yoo"}, yoi{"yoi"};
    Func f;
    Param<int> offset_x{"offset_x"}, offset_y{"offset_y"};
    offset_x.set_range(0, 1);
    offset_y.set_range(0, 1);
    auto idx = [](const auto &x, const auto &y, const auto &offset_x, const auto &offset_y) {
        return (2 * ((y - offset_y) % 2)) + ((x - offset_x) % 2);
    };
    auto a = [](const auto &x, const auto &y) { return x * x; };
    auto b = [](const auto &x, const auto &y) { return x * y; };
    auto c = [](const auto &x, const auto &y) { return y * y; };
    auto d = [](const auto &x, const auto &y) { return x + y; };
    f(x, y) = mux(idx(x, y, offset_x, offset_y), {a(x, y), b(x, y), c(x, y), d(x, y)});
    f.output_buffer().dim(0).set_min(0);
    f.output_buffer().dim(1).set_min(0);

    f
        .aligned_split(x, xo, xi, 2, offset_x, Halide::TailStrategy::GuardWithIf)
        .aligned_split(y, yo, yi, 2, offset_y, Halide::TailStrategy::GuardWithIf)
        .never_partition_all()
        .reorder(xi, yi, xo, yo)
        .unroll(xi)
        .unroll(yi);
    if (gpu) {
        f.gpu_tile(xo, yo, xoo, yoo, xoi, yoi, 4, 4);
    } else {
        f.parallel(yo);
    }

    // The GPU kernels are compiled to device code by the time we get a
    // Module, so only the CPU schedule is inspected for loops and muxes.
    MuxCounter checker;
    if (!gpu) {
        Module module = f.compile_to_module({offset_x, offset_y});
        for (const LoweredFunc &f : module.functions()) {
            f.body.accept(&checker);
        }
    }

    for (int i = 0; i < 4; i++) {
        printf("Testing runtime alignment: x=%d y=%d\n", i / 2, i % 2);
        offset_x.set(i / 2);
        offset_y.set(i % 2);
        Buffer<int> im = f.realize({32, 32});
        if (!gpu) {
            f.realize(im, get_target_from_environment());
        }
        im.copy_to_host();

        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                int selector = idx(2 + x, 2 + y, offset_x.get(), offset_y.get());
                int expected = std::vector<std::function<int(int, int)>>{a, b, c, d}[selector](x, y);
                if (im(x, y) != expected) {
                    printf("im(%d, %d) = %d instead of %d (selector: %d)\n", x, y, im(x, y), expected, selector);
                    return 1;
                }
            }
        }
    }

    if (gpu) {
        return 0;
    }
    if (checker.mux_count != 0) {
        std::printf("Expected 0 muxes: %d\n", checker.mux_count);
        return 1;
    }
    if (checker.for_count != 1) {
        std::printf("Expected 3 for loops: %d\n", checker.for_count);
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
