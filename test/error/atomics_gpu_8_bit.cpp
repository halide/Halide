#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestAtomicsGPU8Bit() {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = cast<uint8_t>(0);
    hist(im(r)) += cast<uint8_t>(1);

    hist.compute_root();

    RVar ro, ri;
    hist.update()
        .atomic()
        .split(r, ro, ri, 8)
        .gpu_blocks(ro)
        .gpu_threads(ri);

    // GPU doesn't support 8/16-bit atomics
    Realization out = hist.realize({hist_size});
}
}  // namespace

TEST(ErrorTests, AtomicsGPU8Bit) {
    const Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }
    if (t.has_feature(Target::WebGPU)) {
        GTEST_SKIP() << "WebGPU will (incorrectly) fail here because 8-bit types are currently emulated using atomics.";
    }
    EXPECT_COMPILE_ERROR(
        TestAtomicsGPU8Bit,
        AnyOf(
            HasSubstr("Atomic updates are not supported inside Metal kernels"),
            HasSubstr("OpenCL only support 32 and 64 bit atomics.")));
}
