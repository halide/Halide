#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

class CountBarriers : public IRVisitor {
public:
    int count;

    CountBarriers()
        : count(0) {
    }

protected:
    using IRVisitor::visit;

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::gpu_thread_barrier)) {
            count++;
        }
        IRVisitor::visit(op);
    }
};

class CheckBarrierCount : public IRMutator {
    int correct;

public:
    CheckBarrierCount(int correct)
        : correct(correct) {
    }
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        CountBarriers c;
        s.accept(&c);

        if (c.count != correct) {
            printf("There were %d barriers. There were supposed to be %d\n", c.count, correct);
            exit(1);
        }

        return s;
    }
};

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    {
        Func f;
        Var x, y;

        // Construct a Func with lots of potential race conditions, and
        // then run it in thread blocks on the gpu.

        f(x, y) = x + 100 * y;

        const int passes = 10;
        for (int i = 0; i < passes; i++) {
            RDom rx(0, 10);
            // Flip each row, using spots 10-19 as temporary storage
            f(rx + 10, y) = f(9 - rx, y);
            f(rx, y) = f(rx + 10, y);
            // Flip each column the same way
            RDom ry(0, 8);
            f(x, ry + 8) = f(x, 7 - ry);
            f(x, ry) = f(x, ry + 8);
        }

        Func g;
        g(x, y) = f(0, 0) + f(9, 7);

        Var xi, yi;
        g.gpu_tile(x, y, xi, yi, 16, 8);
        f.compute_at(g, x);

        for (int i = 0; i < passes; i++) {
            f.update(i * 4 + 0).gpu_threads(y);
            f.update(i * 4 + 1).gpu_threads(y);
            f.update(i * 4 + 2).gpu_threads(x);
            f.update(i * 4 + 3).gpu_threads(x);
        }

        Buffer<int> out = g.realize({100, 100});
        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                int correct = 7 * 100 + 9;
                if (out(x, y) != correct) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, out(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        // Construct a Func with undef stages, then run it in thread
        // blocks and make sure the right number of syncthreads are
        // added.

        Func f;
        Var x, y;
        f(x, y) = 0;
        f(x, y) += undef<int>();
        f(x, y) += x + 100 * y;
        // This next line is dubious, because it entirely masks the
        // effect of the previous definition. If you add an undefined
        // value to the previous def, then Halide can evaluate this to
        // whatever it likes. Currently we'll just elide this update
        // definition.
        f(x, y) += undef<int>();
        f(x, y) += y * 100 + x;

        Func g;
        g(x, y) = f(0, 0) + f(7, 7);

        Var xi, yi;
        g.gpu_tile(x, y, xi, yi, 8, 8);
        f.compute_at(g, x);

        f.gpu_threads(x, y);
        f.update(0).gpu_threads(x, y);
        f.update(1).gpu_threads(x, y);
        f.update(2).gpu_threads(x, y);

        // There should be three thread barriers: one after the intial
        // pure definition, one in between the
        // non-undef definitions, and one between f and g.
        g.add_custom_lowering_pass(new CheckBarrierCount(3));

        Buffer<int> out = g.realize({100, 100});
    }

    printf("Success!\n");
    return 0;
}
