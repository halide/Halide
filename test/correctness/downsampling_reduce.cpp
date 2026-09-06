#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

// Test that an atomic vectorized reduction with a downsampling write pattern
// (f(r/4) += g(r)) lowers to within-vector reductions rather than
// scalarizing. This exercises MultiRamp::div in the vectorize path.

int main(int argc, char **argv) {
    const int vec = 16;
    const int factor = 4;
    const int reduction_extent = vec;  // r has `vec` lanes; f's output has vec/factor

    Func g{"g"};
    Var x{"x"};
    RDom r(0, reduction_extent);

    ImageParam input(Int(32), 1);
    Buffer<int> input_buf(reduction_extent);
    input_buf.for_each_element([&](int i) { input_buf(i) = i * 3 + 7; });
    input.set(input_buf);

    // f(r/4) += g(r): four consecutive lanes of the reduction contribute to
    // one output lane. Within one vector of r, the output multiramp has a
    // stride-zero innermost dim of extent `factor` and a stride-1 outer dim
    // of extent vec/factor.
    g(x) = 0;
    g(r / factor) += input(r);

    Buffer<int> correct = g.realize({reduction_extent / factor});

    g.bound(x, 0, reduction_extent / factor)
        .update()
        .atomic()
        .vectorize(r);

    // Check that the reduction over r was vectorized away: after vectorize,
    // there should be no inner for-loop over r, and the lowered IR should
    // contain a VectorReduce node.
    int inner_for_loops = 0;
    int vector_reduces = 0;
    auto checker = LambdaMutator{
        [&](auto *self, const For *op) {
            if (op->name.find("r") != std::string::npos) {
                inner_for_loops++;
            }
            return self->visit_base(op);
        },
        [&](auto *self, const VectorReduce *op) {
            vector_reduces++;
            return self->visit_base(op);
        }};
    g.add_custom_lowering_pass(&checker, nullptr);

    Buffer<int> out = g.realize({reduction_extent / factor});

    for (int i = 0; i < reduction_extent / factor; i++) {
        if (out(i) != correct(i)) {
            printf("out(%d) = %d instead of %d\n", i, out(i), correct(i));
            return 1;
        }
    }

    if (inner_for_loops > 0) {
        printf("Atomic vectorization of downsampling reduction failed: "
               "lowered code contained %d for loop(s) over r\n",
               inner_for_loops);
        return 1;
    }

    if (vector_reduces == 0) {
        printf("Expected a VectorReduce node in the lowered IR, but "
               "didn't find one\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
