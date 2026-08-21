#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <string>

using namespace Halide;

int main(int argc, char **argv) {
    const Target sve2("arm-64-linux-arm_dot_prod-arm_fp16-sve2-vector_bits_128");
    std::string tmpdir = Internal::get_test_tmp_dir();

    // Dense stores with non-natural lane counts force predicate tail masking.
    // The predicate is a boolean (i1) vector that must be converted from fixed
    // to scalable, which previously triggered an LLVM assertion in
    // getVectorSubVecPointer ("Converting bits to bytes lost precision")
    // because the byte offset computation truncates for i1 (1/8=0).
    Func f("dense_pred_store");
    Var x("x");
    f(x) = cast<uint8_t>(x * 2);
    f.vectorize(x, 24);  // 24 is not a multiple of 16 (natural for uint8 @ 128-bit SVE)
    f.compile_to_object(tmpdir + "sve_dense_pred_store.o", {}, "dense_pred_store", sve2);

    printf("Success!\n");
    return 0;
}
