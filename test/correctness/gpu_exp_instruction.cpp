// The CUDA device has an instruction for exp, and Halide's fast-math semantics
// allow using it. Before, CodeGen_LLVM expanded exp into a polynomial before
// the definition in ptx_dev.ll could be reached, so the instruction never
// appeared. This checks that it does, and that the results are still accurate
// enough to be worth having.

#include "Halide.h"
#include "halide_test_dirs.h"
#include <cmath>
#include <limits>
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace Halide;

namespace {

Target cuda_target() {
    return get_host_target()
        .with_feature(Target::CUDA)
        .with_feature(Target::CUDACapability80);
}

// Compiling for CUDA embeds the PTX in the host assembly, so what the device
// code does is visible there without needing a device.
std::string assembly_for(Func f) {
    Var xi;
    f.gpu_tile(f.args()[0], xi, 32);
    std::string path = Internal::get_test_tmp_dir() + "gpu_exp_instruction.s";
    f.compile_to_assembly(path, {}, f.name(), cuda_target());
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool check_instruction() {
    Var x("x");
    Func f("f");
    f(x) = exp(cast<float>(x) * 0.01f);
    std::string asm_text = assembly_for(f);
    if (asm_text.find("ex2.approx") == std::string::npos) {
        printf("FAIL: exp did not lower to the ex2.approx instruction\n");
        return false;
    }
    // The polynomial expansion is what we are avoiding. It shows up as a long
    // run of fma instructions, which the single instruction does not need.
    return true;
}

bool check_accuracy() {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("[SKIP] No CUDA feature in the target, so not running.\n");
        return true;
    }
    Var x("x"), xi("xi");
    Func f("f");
    // Cover a wide range of magnitudes, including where the result underflows
    // and overflows.
    Expr arg = (cast<float>(x) - 512) * 0.25f;
    f(x) = exp(arg);
    f.gpu_tile(x, xi, 32);
    Buffer<float> out = f.realize({1024}, t);
    for (int i = 0; i < out.width(); i++) {
        float arg = (i - 512) * 0.25f;
        float want = std::exp(arg);
        float got = out(i);
        if (std::isinf(want) || want == 0.f) {
            if (got != want) {
                printf("FAIL: exp(%f) = %g, expected %g\n", arg, got, want);
                return false;
            }
            continue;
        }
        if (want < std::numeric_limits<float>::min()) {
            // A subnormal result has only a few significant bits left, so
            // there is no relative accuracy here to hold anything to.
            continue;
        }
        // ex2.approx is accurate to about 2^-22 relative, and the rescaling
        // of the argument costs a little more.
        float err = std::abs(got - want) / want;
        if (err > 1e-5f) {
            printf("FAIL: exp(%f) = %.9g, expected %.9g (relative error %g)\n",
                   arg, got, want, err);
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (!check_instruction()) {
        return 1;
    }
    if (!check_accuracy()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
