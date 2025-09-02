#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

// Implements a simple gather pipeline to make use of VTCM available on v65+
// hexagon DSP.

// With hexagon targets >=v65 with hvx, we expect to see gathers for
// uint16_t, int16_t, uint32_t, int32_t
// For targets <v65 with hvx, we should generate dynamic_shuffle which are
// compiled to vlut instructions.

// TODO: Test that the expected instructions are seen.

namespace {
template<typename>
class GatherTypedTest : public ::testing::Test {};

using GatherTestTypes = ::testing::Types<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t>;
TYPED_TEST_SUITE(GatherTypedTest, GatherTestTypes);
}  // namespace

TYPED_TEST(GatherTypedTest, Basic) {
    const Target target = get_jit_target_from_environment();
    const int W_img = 128;
    const int H_img = 8;
    const int W_lut = 256;
    const int H_lut = (target.has_feature(Target::HVX_v65)) ? 32 : 1;

    std::mt19937 rng(0xC0FFEEu);
    auto rand_index = [&](int max_inclusive) {
        std::uniform_int_distribution<TypeParam> d(0, max_inclusive);
        return d(rng);
    };

    // Separate channel for xCoord and yCoord for LUT index.
    Buffer<TypeParam> input(W_img, 2);
    for (int x = 0; x < W_img; x++) {
        input(x, 0) = rand_index(W_lut - 1);
        input(x, 1) = rand_index(H_lut - 1);
    }
    // Two Dimensional LUT.
    Buffer<TypeParam> lut(W_lut, H_lut);
    std::uniform_int_distribution<TypeParam> d(
        std::numeric_limits<TypeParam>::lowest(), std::numeric_limits<TypeParam>::max());
    for (int y = 0; y < H_lut; y++) {
        for (int x = 0; x < W_lut; x++) {
            lut(x, y) = d(rng);
        }
    }

    Var x, y;
    Func lut_vtcm, output_vtcm, output;

    // Implement: output(x, y) = lut(input(x, 0), input(x, 1))
    // output and lut must have store_in(MemoryType::VTCM) to generate vgathers.
    Expr xCoord = clamp(cast<int32_t>(input(x, 0)), 0, W_lut - 1);
    Expr yCoord = clamp(cast<int32_t>(input(x, 1)), 0, H_lut - 1);
    lut_vtcm(x, y) = lut(x, y);
    output_vtcm(x, y) = lut_vtcm(xCoord, yCoord);
    output(x, y) = output_vtcm(x, y);

    if (target.has_feature(Target::HVX)) {
        const int vector_size = target.has_feature(Target::HVX) ? 128 : 64;
        Var yi;

        output
            .hexagon()
            .split(y, y, yi, H_img / 2)
            .parallel(y)
            .vectorize(x, vector_size);

        if (target.features_any_of({Target::HVX_v65, Target::HVX_v66,
                                    Target::HVX_v68})) {
            lut_vtcm
                .store_in(MemoryType::VTCM)
                .compute_at(output, Var::outermost())
                .vectorize(x, vector_size);

            output_vtcm
                .store_in(MemoryType::VTCM)
                .compute_at(output, y)
                .vectorize(x, vector_size);
        }
    }

    Buffer<TypeParam> output_buf = output.realize({W_img, H_img});

    for (int yy = 0; yy < H_img; yy++) {
        for (int xx = 0; xx < W_img; xx++) {
            int xi = std::max(std::min(static_cast<int>(input(xx, 0)), W_lut - 1), 0);
            int yi = std::max(std::min(static_cast<int>(input(xx, 1)), H_lut - 1), 0);
            TypeParam correct = lut(xi, yi);
            EXPECT_EQ(output_buf(xx, yy), correct)
                << "x = " << xx << ", y = " << yy;
        }
    }
}
