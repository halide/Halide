#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class CheckThreadExtent : public IRVisitor {
    using IRVisitor::visit;
    void visit(const For *op) override {
        if ((op->name == ".__thread_id_x") || (op->name == ".__thread_id_y")) {
            assert(op->for_type == ForType::GPUThread);
            // Assert the min and extent to be 0 and 16 for this particular test case
            const int64_t *min = as_const_int(op->min);
            const int64_t *extent = as_const_int(op->extent);
            assert(min && (*min == 0));
            assert(extent && (*extent == 16));
        }
        IRVisitor::visit(op);
    }
};

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        return 0;
    }

    Var x("x"), y("y"), bx("bx"), by("by"), tx("tx"), ty("ty");

    Param<int> width("width"), height("height");
    ImageParam input(Int(32), 2, "input");

    Func tuple("tuple");
    tuple(x, y) = Tuple(input(x, y), input(x, y));

    Func consumer("consumer");
    consumer(x, y) = input(x, y) + tuple(x, y)[0];

    input.dim(0).set_bounds(0, width).dim(1).set_bounds(0, height).set_stride(width);

    // Schedule
    consumer.compute_root()
        .bound(x, 0, width)
        .bound(y, 0, height)
        .vectorize(x, 4, TailStrategy::ShiftInwards)
        .tile(x, y, bx, by, tx, ty, 16, 16, TailStrategy::ShiftInwards)
        .gpu_blocks(bx, by)
        .gpu_threads(tx, ty);

    tuple.compute_at(consumer, bx)
        .vectorize(x, 4, TailStrategy::RoundUp)
        .gpu_threads(x, y);

    // Lower it and inspect the IR to verify the min/extent of GPU ".__thread_id_x"
    Module m = consumer.compile_to_module({consumer.infer_arguments()}, "fuse_gpu_threads", target);
    CheckThreadExtent c;
    m.functions().front().body.accept(&c);

    return 0;
}
