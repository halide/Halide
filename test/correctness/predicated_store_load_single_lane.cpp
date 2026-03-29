#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // SVE2 backend has the below LLVM issue which has been fixed in LLVM 22.
    // "LLVM ERROR: Unable to widen vector store"
    // https://github.com/llvm/llvm-project/issues/54424
    if (Internal::get_llvm_version() < 220 &&
        get_jit_target_from_environment().has_feature(Target::SVE2)) {
        printf("[SKIP] LLVM %d has known SVE backend bugs for this test.\n",
               Internal::get_llvm_version());
        return 0;
    }

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
