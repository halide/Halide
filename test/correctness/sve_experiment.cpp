#include "Halide.h"

#include <stdlib.h>

using namespace Halide;

int main(int argc, char *argv[]) {
    Target t("arm-64-linux-sve2-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt-vector_bits_512");
    int vectorize_amount = t.natural_vector_size<uint8_t>();

    Var x{"x"};
    ImageParam in_a(UInt(8), 1, "in_a");
    ImageParam in_b(UInt(8), 1, "in_b");

    Func result("result");

    result(x) = in_a(x) + in_b(x);
    if (vectorize_amount != 0) {
      result.vectorize(x, vectorize_amount);
    }

    result.compile_to_llvm_assembly("/tmp/sve_experiment.ll", { in_a, in_b }, "vec_add_1d", t);
    result.compile_to_assembly("/tmp/sve_experiment.s", { in_a, in_b }, "vec_add_1d", t);
    
    return 0;
}
