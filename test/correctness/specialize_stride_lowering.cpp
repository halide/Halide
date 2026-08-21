#include "Halide.h"
#include <cstdio>

using namespace Halide;

// Regression test for https://github.com/halide/Halide/pull/8858 ("Change
// for loops from min/extent to min/max"): a Func with an unconstrained
// output-buffer stride, specialized on stride==1, silently loses that
// specialization during lowering -- every call falls through to the
// generic (always-strided) path, even when the buffer really is contiguous.
//
// The condition itself has no effect on the IR this early in lowering (the
// stride only shows up once storage flattening turns indices into flat
// addresses), so the specialized and generic branches are textually
// identical when Simplify runs. That made Simplify's "merge branches with
// equal bodies" rule collapse them into one, discarding the specialization.
//
// dst.parallel(y) makes each surviving specialize() branch lower to its own
// distinct parallel-closure helper function; dropping the specialization
// collapses that back down to a single generic function. Counting those
// functions is a precise, non-textual way to detect whether the
// specialization survived lowering. Not target-specific: reproduces under
// the default JIT/host target.
int main(int argc, char **argv) {
    ImageParam src(UInt(8), 2, "src");

    Var x("x"), y("y");

    Func dst("dst");
    dst(x, y) = src(x, y);

    dst.output_buffer().dim(0).set_stride(Expr());

    dst.parallel(y);

    dst.specialize(dst.output_buffer().dim(0).stride() == 1);

    Module m = dst.compile_to_module({src}, "dst", get_jit_target_from_environment());

    int par_for_count = 0;
    for (const auto &f : m.functions()) {
        if (f.name.find("_par_for_") != std::string::npos) {
            par_for_count++;
        }
    }

    // Expect 2: one for the specialized branch, one for the generic fallback.
    if (par_for_count <= 1) {
        printf("Specialize() branch did not survive lowering: expected "
               "2 distinct parallel-closure functions, found %d\n",
               par_for_count);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
