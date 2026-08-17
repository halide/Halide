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

int main(int argc, char **argv) {
    Var x{"x"}, xo{"xo"}, xi{"xi"};
    for (auto ts : {TailStrategy::ShiftInwards, TailStrategy::GuardWithIf}) {
        Func f;
        Param<int> offset{"offset"};
        offset.set_range(0, 3);
        f(x) = mux((x - offset) % 4, {x, x * x, 2 * x, -x * (x + 1)});
        f.output_buffer().dim(0).set_min(0);
        f
            .split(x, xo, xi, 4, offset, ts)
            .unroll(xi);

        Module module = f.compile_to_module({offset});
        MuxCounter checker;
        for (const LoweredFunc &f : module.functions()) {
            f.body.accept(&checker);
        }

        for (int i = 0; i < 4; i++) {
            printf("Testing runtime alignment: %d\n", i);
            offset.set(i);
            Buffer<int> im = f.realize({32});
            f.realize(im, get_target_from_environment());

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

    printf("Success!\n");
    return 0;
}
