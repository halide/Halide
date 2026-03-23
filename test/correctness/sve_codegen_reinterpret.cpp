#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <string>

using namespace Halide;

int main(int argc, char **argv) {
    const Target sve2("arm-64-linux-arm_dot_prod-arm_fp16-sve2-vector_bits_128");
    std::string tmpdir = Internal::get_test_tmp_dir();

    // Reinterpret between Handle (pointer) and integer types with vectorization.
    // Pointers produce fixed vectors (<4 x ptr>) while the integer destination
    // may be scalable (<vscale x 4 x i64>), requiring conversion before the
    // cast. Previously triggered ConstantExpr::getCast ("Invalid constantexpr
    // cast!") because CreateBitOrPointerCast cannot operate across fixed and
    // scalable vector types, and fixed_to_scalable_vector_type passed the wrong
    // value to the llvm.vector.insert intrinsic.
    std::string msg = "hello!\n";
    Func f("handle_cast"), g("copy"), h("out");
    Var x("x");
    f(x) = cast<char *>(msg);
    f.compute_root().vectorize(x, 4);
    g(x) = f(x);
    g.compute_root();
    h(x) = g(x);
    h.compile_to_object(tmpdir + "sve_handle_cast.o", {}, "handle_cast", sve2);

    printf("Success!\n");
    return 0;
}
