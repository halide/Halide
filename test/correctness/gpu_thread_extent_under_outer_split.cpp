#include "Halide.h"
#include <iostream>

using namespace Halide;
using namespace Halide::Internal;

// A producer computed at the gpu_blocks of a gpu_tile'd consumer, where that
// consumer is itself computed inside an outer GuardWithIf split. The thread
// loop of the producer spans one tile of the consumer, so the fused thread
// block size must fold to the tile size (16). Reduced from a NeonRAW BLADE
// pipeline, where the block size instead depended on the image height and
// exceeded the device's limit on threads per block.

class CheckConstantThreadExtents : public IRMutator {
public:
    using IRMutator::mutate;
    int errors = 0;

    Stmt mutate(const Stmt &s) override {
        visit_with(s, [&](auto *self, const For *op) {
            if (op->for_type == ForType::GPUThread) {
                auto m = as_const_int(op->max);
                if (!m || *m >= 16) {
                    std::cout << "Bad gpu_thread loop " << op->name << ": max = " << op->max << "\n";
                    errors++;
                }
            }
            self->visit_base(op);
        });
        return s;
    }
};

int main(int argc, char **argv) {
    Var x{"x"}, y{"y"}, xo{"xo"}, yo{"yo"}, xi{"xi"}, yi{"yi"}, yso{"yso"};
    Func producer{"producer"}, consumer{"consumer"}, out{"out"};
    producer(x, y) = x + y;
    consumer(x, y) = producer(x, y);
    out(x, y) = consumer(x, y) + consumer(x, y + 1);

    out.compute_root()
        .split(y, yso, y, 1024, TailStrategy::GuardWithIf);
    consumer.compute_at(out, yso)
        .gpu_tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);
    producer.compute_at(consumer, xo)
        .gpu_threads(x, y);

    // Only compiles, so no GPU is needed.
    Target t = get_host_target().with_feature(Target::CUDA);
    CheckConstantThreadExtents checker;
    out.add_custom_lowering_pass(&checker, nullptr);
    out.compile_to_module({}, "gpu_thread_extent_under_outer_split", t);
    if (checker.errors) {
        printf("%d gpu_thread loops without a constant extent below 16\n", checker.errors);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
