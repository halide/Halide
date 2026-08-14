#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {

class CheckVectorStoreAlignment : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Store *op) override {
        if (starts_with(op->name, "output") && op->value.type().lanes() == 32) {
            found_vector_store = true;
            all_vector_stores_aligned &= op->alignment.modulus % 32 == 0 &&
                                         op->alignment.remainder % 32 == 0;
        }
        IRVisitor::visit(op);
    }

public:
    bool found_vector_store = false;
    bool all_vector_stores_aligned = true;
};

}  // namespace


int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment()
                        //.with_feature(Target::NoAsserts)
                        .with_feature(Target::NoBoundsQuery)
                        .with_feature(Target::NoRuntime);

    // We'll run the test twice, but with a different way of expressing
    // the dense storage constraints:
    //  - once with dim(1).set_stride(dim(0).extent())
    //  - once with add_requirement(dim(1).stride() == dim(0).extent())
    for (int i = 0; i < 2; ++i) {
        Var x{"x"}, y{"y"}, xi{"xi"}, yi{"yi"};
        ImageParam input{UInt(16), 2, "input"};

        Func output{"output"};
        output(x, y) = input(x, y);

        output.align_extent(x, 32)
            .align_extent(y, 4)
            .tile(x, y, xi, yi, 32, 4)
            .vectorize(xi);


        output.output_buffer().set_host_alignment(64);
        if (i == 0) {
            printf("Testing Dimension setting tricks\n");
            output.output_buffer().dim(0).set_min(0);
            output.output_buffer().dim(1).set_min(0);
            output.output_buffer().dim(1).set_stride(output.output_buffer().dim(0).extent());
        }

        Pipeline p(output);
        if (i == 1) {
            printf("Testing add_requirements\n");
            p.add_requirement(output.output_buffer().dim(0).min() == 0);
            p.add_requirement(output.output_buffer().dim(1).min() == 0);
            p.add_requirement(output.output_buffer().dim(1).stride() == output.output_buffer().dim(0).extent());
        }

        Module module = p.compile_to_module({input}, "vector_store_alignment", target);

        CheckVectorStoreAlignment checker;
        for (const LoweredFunc &f : module.functions()) {
            f.body.accept(&checker);
        }

        if (!checker.found_vector_store) {
            printf("Did not find the vectorized output store.\n");
            return 1;
        }
        if (!checker.all_vector_stores_aligned) {
            printf("The vectorized output store was not known to be aligned by 32 elements.\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
