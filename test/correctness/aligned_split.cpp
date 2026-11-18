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
    for (auto ts : {TailStrategy::ShiftInwards, TailStrategy::GuardWithIf}) {
        Func f;
        Param<int> offset{"offset"};
        offset.set_range(0, 3);
        f(x) = mux((x - offset) % 4, {x, x * x, 2 * x, -x * (x + 1)});
        f.output_buffer().dim(0).set_min(0);
        f
            .aligned_split(x, xo, xi, 4, offset, ts)
            .unroll(xi);
        if (gpu) {
            f.gpu_tile(xo, xoo, xoi, 2);
        }

        // The GPU kernels are compiled to device code by the time we get a
        // Module, so only the CPU schedule is inspected for loops and muxes.
        MuxCounter checker;
        if (!gpu) {
            Module module = f.compile_to_module({offset});
            for (const LoweredFunc &f : module.functions()) {
                f.body.accept(&checker);
            }
        }

        for (int i = 0; i < 4; i++) {
            printf("Testing runtime alignment: %d\n", i);
            offset.set(i);
            Buffer<int> im = f.realize({32});
            if (!gpu) {
                f.realize(im, get_target_from_environment());
            }
            im.copy_to_host();

            for (int x = 0; x < 32; x++) {
                int selector = (4 + x - offset.get()) % 4;
                int expected;
                if (selector == 0) {
                    expected = x;
                } else if (selector == 1) {
                    expected = x * x;
                } else if (selector == 2) {
                    expected = 2 * x;
                } else {
                    expected = -x * (x + 1);
                }
                if (im(x) != expected) {
                    printf("im(%d) = %d instead of %d (selector: %d)\n", x, im(x), expected, selector);
                    return 1;
                }
            }
        }

        if (gpu) {
            continue;
        }
        if (ts == Halide::TailStrategy::ShiftInwards) {
            if (checker.mux_count != 8) {
                std::printf("Expected 8 muxes: %d\n", checker.mux_count);
                return 1;
            }
            if (checker.for_count != 1) {
                // The head and tail are reduced to a single iteration, so the loop is stripped.
                std::printf("Expected one for loop: %d\n", checker.for_count);
                return 1;
            }
        } else if (ts == Halide::TailStrategy::GuardWithIf) {
            if (checker.mux_count != 0) {
                std::printf("Expected 0 muxes: %d\n", checker.mux_count);
                return 1;
            }
            if (checker.for_count != 2) {
                // The head and tail are reduced to a single iteration, so the loop is stripped.
                std::printf("Expected one for loop: %d\n", checker.for_count);
                return 1;
            }
        }
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
