// Exercises Func::gpu_min_resident_blocks, which tells the backend how many
// blocks of the kernel it should plan to fit on one processor. It is a hint
// about a tradeoff the backend would otherwise make on its own, so what is
// checked here is that it reaches the generated code, that it does not change
// the answer, and that a nonsensical value is rejected.

#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace Halide;

Func make_pipeline(Var x, Var y) {
    Func f("f");
    f(x, y) = cast<float>(x * 3 + y);
    return f;
}

// Compiling for CUDA embeds the PTX in the host assembly, so the
// .minnctapersm directive is visible there. No device is needed to check this.
std::string assembly_for(int min_blocks) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func f = make_pipeline(x, y);
    f.gpu_tile(x, y, xi, yi, 8, 8);
    if (min_blocks >= 0) {
        f.gpu_min_resident_blocks(DeviceAPI::CUDA, min_blocks);
    }
    Target t = get_host_target()
                   .with_feature(Target::CUDA)
                   .with_feature(Target::CUDACapability80);
    std::string path = Internal::get_test_tmp_dir() + "gpu_min_resident_blocks.s";
    f.compile_to_assembly(path, {}, "f", t);
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool check_directive_reaches_ptx() {
    if (assembly_for(-1).find("minnctapersm") != std::string::npos) {
        printf("FAIL: .minnctapersm appeared without being asked for\n");
        return false;
    }
    if (assembly_for(3).find("minnctapersm 3") == std::string::npos) {
        printf("FAIL: .minnctapersm 3 did not reach the generated PTX\n");
        return false;
    }
    if (assembly_for(0).find("minnctapersm") != std::string::npos) {
        printf("FAIL: gpu_min_resident_blocks(0) still emitted a directive\n");
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
    f.gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::Auto, DeviceAPI::CUDA)
        .gpu_min_resident_blocks(DeviceAPI::CUDA, 2);
    Buffer<float> out = f.realize({64, 64}, t);
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            float want = (float)(x * 3 + y);
            if (out(x, y) != want) {
                printf("FAIL: out(%d, %d) = %f, expected %f\n", x, y, out(x, y), want);
                return false;
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (!check_directive_reaches_ptx()) {
        return 1;
    }
    if (!check_answer()) {
        return 1;
    }

#if HALIDE_WITH_EXCEPTIONS
    bool ok = true;
    for (int bad : {-1, -8}) {
        ok &= expect_user_error("gpu_min_resident_blocks", "gpu_min_resident_blocks", [&]() {
            Var x("x"), y("y"), xi("xi"), yi("yi");
            Func f = make_pipeline(x, y);
            f.gpu_tile(x, y, xi, yi, 8, 8).gpu_min_resident_blocks(DeviceAPI::CUDA, bad);
        });
    }
    if (!ok) {
        return 1;
    }
#endif

    printf("Success!\n");
    return 0;
}
