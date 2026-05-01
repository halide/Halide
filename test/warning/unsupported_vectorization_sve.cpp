#include "Halide.h"
#include "halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = x * 0.1f;

    constexpr int vscale = 2;
    constexpr int vector_bits = 128 * vscale;

    f.vectorize(x, vscale * 3);
    Target t("arm-64-linux-sve2-vector_bits_" + std::to_string(vector_bits));

    // SVE is disabled with user_warning,
    // which would have ended up with emitting <vscale x 3 x float> if we didn't.
    f.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "unused.ll", f.infer_arguments(), "f", t);

    return 0;
}
