#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUF16Intrinsics, Basic) {
    auto target = get_jit_target_from_environment();
    if (!target.has_feature(Target::Metal) &&
        !target.features_all_of({Target::OpenCL, Target::CLHalf})) {
        GTEST_SKIP() << "Test only applies to Metal and OpenCL+CLHalf.";
    }

    Func output, output_cpu;
    Var x, y, xi, xo, yi, yo;
    Expr val = cast(Float(16), cast(Float(16), x + y) + 1.f);
    Expr clamp_val = clamp(cast(Float(16), 0.1f) * val, cast(Float(16), 0), cast(Float(16), 1));

    output(x, y) = cast(Float(16), select(clamp_val > 1, cast<float>(abs(clamp_val)), cast<float>(fast_pow(clamp_val, cast(Float(16), 1.f / 2.2f)))));
    output_cpu(x, y) = cast(Float(16), select(clamp_val > 1, cast<float>(abs(clamp_val)), cast<float>(fast_pow(clamp_val, cast(Float(16), 1.f / 2.2f)))));

    output.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);

    Buffer<float16_t> out = output.realize({64, 64});
    Buffer<float16_t> out2 = output_cpu.realize({64, 64});
    out.copy_to_host();

    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            float a = float(out(i, j));
            float b = float(out2(i, j));
            ASSERT_NEAR(a, b, 0.01f) << "Incorrect value at " << i << "," << j;
        }
    }

    Func f, g, h;

    f(x) = float16_t::make_infinity();
    g(x) = float16_t::make_negative_infinity();
    h(x) = float16_t::make_nan();

    f.gpu_tile(x, xo, xi, 8);
    g.gpu_tile(x, xo, xi, 8);
    h.gpu_tile(x, xo, xi, 8);

    Buffer<float16_t> fout = f.realize({8});
    Buffer<float16_t> gout = g.realize({8});
    Buffer<float16_t> hout = h.realize({8});
    fout.copy_to_host();
    gout.copy_to_host();
    hout.copy_to_host();

    for (int i = 0; i < 8; i++) {
        EXPECT_TRUE(fout(i).is_infinity()) << "Expected infinity at index " << i << ", got: " << fout(i).to_bits();
        EXPECT_TRUE(gout(i).is_infinity() && gout(i).is_negative()) << "Expected negative infinity at index " << i << ", got: " << gout(i).to_bits();
        EXPECT_TRUE(hout(i).is_nan()) << "Expected NaN at index " << i << ", got: " << hout(i).to_bits();
    }
}
