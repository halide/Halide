#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
constexpr int w = 16;
constexpr int h = 16;

/* Halide uses fast-math by default. is_nan must either be used inside
 * strict_float or as a test on inputs produced outside of
 * Halide. Using it to test results produced by math inside Halide but
 * not using strict_float is unreliable. This test covers both of these cases. */

void check_nans(const Buffer<float> &im) {
    for (int x = 0; x < im.dim(0).extent(); x++) {
        for (int y = 0; y < im.dim(1).extent(); y++) {
            if ((x - y) < 0) {
                EXPECT_EQ(im(x, y), 0.0f) << "undetected Nan for sqrt(" << x << " - " << y << ")";
            } else {
                EXPECT_EQ(im(x, y), 1.0f) << "unexpected Nan for sqrt(" << x << " - " << y << ")";
            }
        }
    }
}

void check_infs(const Buffer<float> &im) {
    for (int x = 0; x < im.dim(0).extent(); x++) {
        for (int y = 0; y < im.dim(1).extent(); y++) {
            float e = (float)(x - w / 2) / (float)(y - h / 2);
            if (std::isinf(e)) {
                EXPECT_EQ(im(x, y), 1.0f) << "undetected Inf for (" << x << "-" << w / 2 << ")/(" << y << "-" << h / 2 << ") -> " << e;
            } else {
                EXPECT_EQ(im(x, y), 0.0f) << "unexpected Inf for (" << x << "-" << w / 2 << ")/(" << y << "-" << h / 2 << ") -> " << e;
            }
        }
    }
}

void check_finites(const Buffer<float> &im) {
    for (int x = 0; x < im.dim(0).extent(); x++) {
        for (int y = 0; y < im.dim(1).extent(); y++) {
            float e = (float)(x - w / 2) / (float)(y - h / 2);
            if (std::isfinite(e)) {
                EXPECT_EQ(im(x, y), 1.0f) << "undetected finite for (" << x << "-" << w / 2 << ")/(" << y << "-" << h / 2 << ") -> " << e;
            } else {
                EXPECT_EQ(im(x, y), 0.0f) << "unexpected finite for (" << x << "-" << w / 2 << ")/(" << y << "-" << h / 2 << ") -> " << e;
            }
        }
    }
}
}  // namespace

class IsNanTest : public ::testing::Test {
protected:
    Var x, y;
    void SetUp() override {
        if (get_jit_target_from_environment().has_feature(Target::WebGPU)) {
            GTEST_SKIP() << "WebGPU does not reliably support isnan, isinf, or isfinite.";
        }
    }
};

TEST_F(IsNanTest, IsNanWithStrictFloat) {
    Func f;
    Expr e = sqrt(x - y);
    f(x, y) = strict_float(select(is_nan(e), 0.0f, 1.0f));
    f.vectorize(x, 8);

    Buffer<float> im = f.realize({w, h});
    check_nans(im);
}

TEST_F(IsNanTest, IsNanWithExternalData) {
    Buffer<float> non_halide_produced(w, h);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            non_halide_produced(x, y) = sqrt(x - y);
        }
    }

    ImageParam in(Float(32), 2);
    Func f;
    f(x, y) = select(is_nan(in(x, y)), 0.0f, 1.0f);
    f.vectorize(x, 8);

    in.set(non_halide_produced);
    Buffer<float> im = f.realize({w, h});
    check_nans(im);
}

TEST_F(IsNanTest, IsInfWithStrictFloat) {
    Func f;
    Expr e = cast<float>(x - w / 2) / cast<float>(y - h / 2);
    f(x, y) = strict_float(select(is_inf(e), 1.0f, 0.0f));
    f.vectorize(x, 8);

    Buffer<float> im = f.realize({w, h});
    check_infs(im);
}

TEST_F(IsNanTest, IsInfWithExternalData) {
    Buffer<float> non_halide_produced(w, h);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            non_halide_produced(x, y) = (float)(x - w / 2) / (float)(y - h / 2);
        }
    }

    ImageParam in(Float(32), 2);
    Func f;
    f(x, y) = select(is_inf(in(x, y)), 1.0f, 0.0f);
    f.vectorize(x, 8);

    in.set(non_halide_produced);
    Buffer<float> im = f.realize({w, h});
    check_infs(im);
}

TEST_F(IsNanTest, IsFiniteWithStrictFloat) {
    Func f;
    Expr e = cast<float>(x - w / 2) / cast<float>(y - h / 2);
    f(x, y) = strict_float(select(is_finite(e), 1.0f, 0.0f));
    f.vectorize(x, 8);

    Buffer<float> im = f.realize({w, h});
    check_finites(im);
}

TEST_F(IsNanTest, IsFiniteWithExternalData) {
    Buffer<float> non_halide_produced(w, h);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            non_halide_produced(x, y) = (float)(x - w / 2) / (float)(y - h / 2);
        }
    }

    ImageParam in(Float(32), 2);
    Func f;
    f(x, y) = select(is_finite(in(x, y)), 1.0f, 0.0f);
    f.vectorize(x, 8);

    in.set(non_halide_produced);
    Buffer<float> im = f.realize({w, h});
    check_finites(im);
}

TEST_F(IsNanTest, IsNanWithStrictFloatGPU) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "GPU not available";
    }

    Func f;
    Var tx, ty;
    Expr e = sqrt(x - y);
    f(x, y) = strict_float(select(is_nan(e), 0.0f, 1.0f));
    f.gpu_tile(x, y, tx, ty, 8, 8);

    Buffer<float> im = f.realize({w, h});
    check_nans(im);
}

TEST_F(IsNanTest, IsNanWithExternalDataGPU) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "GPU not available";
    }

    Buffer<float> non_halide_produced(w, h);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            non_halide_produced(x, y) = sqrt(x - y);
        }
    }

    ImageParam in(Float(32), 2);
    Func f;
    Var tx, ty;
    f(x, y) = select(is_nan(in(x, y)), 0.0f, 1.0f);
    f.gpu_tile(x, y, tx, ty, 8, 8);

    in.set(non_halide_produced);
    Buffer<float> im = f.realize({w, h});
    check_nans(im);
}

TEST_F(IsNanTest, IsInfWithStrictFloatGPU) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "GPU not available";
    }

    Func f;
    Var tx, ty;
    Expr e = cast<float>(x - w / 2) / cast<float>(y - h / 2);
    f(x, y) = strict_float(select(is_inf(e), 1.0f, 0.0f));
    f.gpu_tile(x, y, tx, ty, 8, 8);

    Buffer<float> im = f.realize({w, h});
    check_infs(im);
}

TEST_F(IsNanTest, IsInfWithExternalDataGPU) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "GPU not available";
    }

    Buffer<float> non_halide_produced(w, h);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            non_halide_produced(x, y) = (float)(x - w / 2) / (float)(y - h / 2);
        }
    }

    ImageParam in(Float(32), 2);
    Func f;
    Var tx, ty;
    f(x, y) = select(is_inf(in(x, y)), 1.0f, 0.0f);
    f.gpu_tile(x, y, tx, ty, 8, 8);

    in.set(non_halide_produced);
    Buffer<float> im = f.realize({w, h});
    check_infs(im);
}

TEST_F(IsNanTest, IsFiniteWithStrictFloatGPU) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "GPU not available";
    }

    Func f;
    Var tx, ty;
    Expr e = cast<float>(x - w / 2) / cast<float>(y - h / 2);
    f(x, y) = strict_float(select(is_finite(e), 1.0f, 0.0f));
    f.gpu_tile(x, y, tx, ty, 8, 8);

    Buffer<float> im = f.realize({w, h});
    check_finites(im);
}

TEST_F(IsNanTest, IsFiniteWithExternalDataGPU) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "GPU not available";
    }

    Buffer<float> non_halide_produced(w, h);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            non_halide_produced(x, y) = (float)(x - w / 2) / (float)(y - h / 2);
        }
    }

    ImageParam in(Float(32), 2);
    Func f;
    Var tx, ty;
    f(x, y) = select(is_finite(in(x, y)), 1.0f, 0.0f);
    f.gpu_tile(x, y, tx, ty, 8, 8);

    in.set(non_halide_produced);
    Buffer<float> im = f.realize({w, h});
    check_finites(im);
}
