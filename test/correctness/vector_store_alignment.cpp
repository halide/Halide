#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {

class CheckVectorStoreAlignment : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Store *op) override {
        if (op->name == "output" && op->value.type().lanes() == 32) {
            found_vector_store = true;
            alignment_is_known = op->alignment.modulus % 32 == 0 &&
                                 op->alignment.remainder % 32 == 0;
        }
        IRVisitor::visit(op);
    }

public:
    bool found_vector_store = false;
    bool alignment_is_known = false;
};

}  // namespace

int main(int argc, char **argv) {
    Var x{"x"}, y{"y"}, xi{"xi"}, yi{"yi"};
    ImageParam input{UInt(16), 2, "input"};

    Func output{"output"};
    output(x, y) = input(x, y);

    output.align_extent(x, 32)
        .tile(x, y, xi, yi, 32, 4)
        .vectorize(xi);

    output.output_buffer().dim(0).set_min(0);
    output.output_buffer().dim(1).set_min(0);
    output.output_buffer().dim(1).set_stride(output.output_buffer().dim(0).extent());
    output.output_buffer().set_host_alignment(64);

    Target target = get_jit_target_from_environment()
                        .with_feature(Target::NoAsserts)
                        .with_feature(Target::NoBoundsQuery)
                        .with_feature(Target::NoRuntime);
    Module module = output.compile_to_module({input}, "vector_store_alignment", target);

    CheckVectorStoreAlignment checker;
    for (const LoweredFunc &f : module.functions()) {
        f.body.accept(&checker);
    }

    if (!checker.found_vector_store) {
        printf("Did not find the vectorized output store.\n");
        return 1;
    }
    if (!checker.alignment_is_known) {
        printf("The vectorized output store was not known to be aligned by 32 elements.\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
