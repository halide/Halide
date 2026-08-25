// Exercises Func::gpu_max_registers, which caps the registers a thread of the
// kernel may use. The cap is a hint to the backend compiler about a tradeoff it
// would otherwise make on its own, so what is checked here is that it reaches
// the generated code, that it does not change the answer, and that a
// nonsensical cap is rejected.

#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"
#include <stdio.h>
#include <fstream>
#include <sstream>

using namespace Halide;

namespace {

// A kernel with enough live values to have an opinion about registers.
Func make_pipeline(Var x, Var y) {
    Func f("f");
    Expr e = cast<float>(x + y);
    for (int i = 0; i < 8; i++) {
        e = e * e + cast<float>(x - i) - cast<float>(y + i);
    }
    f(x, y) = e;
    return f;
}

float expected(int x, int y) {
    float e = (float)(x + y);
    for (int i = 0; i < 8; i++) {
        e = e * e + (float)(x - i) - (float)(y + i);
    }
    return e;
}

// Compiling for CUDA embeds the PTX in the host assembly, so the .maxnreg
// directive is visible there. No device is needed to check this.
std::string assembly_for(int max_registers) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func f = make_pipeline(x, y);
    f.gpu_tile(x, y, xi, yi, 8, 8);
    if (max_registers >= 0) {
        f.gpu_max_registers(max_registers);
    }
    Target t = get_host_target()
                   .with_feature(Target::CUDA)
                   .with_feature(Target::CUDACapability80);
    std::string path = Internal::get_test_tmp_dir() + "gpu_max_registers.s";
    f.compile_to_assembly(path, {}, "f", t);
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool check_directive_reaches_ptx() {
    if (assembly_for(-1).find("maxnreg") != std::string::npos) {
        printf("FAIL: .maxnreg appeared without being asked for\n");
        return false;
    }
    if (assembly_for(40).find("maxnreg 40") == std::string::npos) {
        printf("FAIL: .maxnreg 40 did not reach the generated PTX\n");
        return false;
    }
    // Zero asks for the automatic choice, which is the same as not asking.
    if (assembly_for(0).find("maxnreg") != std::string::npos) {
        printf("FAIL: gpu_max_registers(0) still capped the registers\n");
        return false;
    }
    return true;
}

bool check_answer() {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("[SKIP] Not running the pipeline: target has no CUDA feature.\n");
        return true;
    }
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func f = make_pipeline(x, y);
    f.gpu_tile(x, y, xi, yi, 8, 8).gpu_max_registers(32);
    Buffer<float> out = f.realize({64, 64}, t);
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            float want = expected(x, y);
            if (out(x, y) != want) {
                printf("FAIL: out(%d, %d) = %f, expected %f\n", x, y, out(x, y), want);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (!check_directive_reaches_ptx()) {
        return 1;
    }
    if (!check_answer()) {
        return 1;
    }

#if HALIDE_WITH_EXCEPTIONS
    // A negative number of registers means nothing, so it is rejected rather
    // than quietly treated as zero.
    bool ok = true;
    for (int bad : {-1, -8}) {
        ok &= expect_user_error("gpu_max_registers", "gpu_max_registers", [&]() {
            Var x("x"), y("y"), xi("xi"), yi("yi");
            Func f = make_pipeline(x, y);
            f.gpu_tile(x, y, xi, yi, 8, 8).gpu_max_registers(bad);
        });
    }
    if (!ok) {
        return 1;
    }
#endif

    printf("Success!\n");
    return 0;
}
