#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // This test exercises predicated vector loads and stores with a single
    // lane. These require special handling because Halide's IR does not
    // distinguish between scalars and single-element vectors, while LLVM
    // does.

    int w = get_jit_target_from_environment().natural_vector_size<float>();

    Func f1{"f1"}, f2{"f2"};
    Var x{"x"}, xo{"xo"}, xi{"xi"};

    ImageParam input(Float(32), 1);

    f1(x) = input(x) * 2;
    f2(x) = select(x < w, 0, f1(x) + f1(x + 1));

    // This schedule creates a situation where f1 is computed with a
    // vectorized loop that requires predicated loads/stores for the
    // final single element.
    f2.split(x, xo, xi, w);
    f1.compute_at(f2, xo).vectorize(x);  // effective vector width = w + 1

    // Compile to check that codegen succeeds. This would crash before the fix
    // with "Call parameter type does not match function signature" because
    // the masked load/store intrinsics received scalar masks instead of
    // vector masks.
    f2.compile_jit();

    printf("Success!\n");
    return 0;
}
