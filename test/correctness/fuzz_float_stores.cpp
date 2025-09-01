#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Target target = get_jit_target_from_environment();
Target target_fuzzed = target.with_feature(Target::FuzzFloatStores);
constexpr int kSize = 1000;
}  // namespace

TEST(FuzzFloatStoresTest, Unaffected) {
    // Check some code that should be unaffected
    Func f;
    Var x;
    f(x) = (x - 42.5f) / 16.0f;
    f.vectorize(x, 8);

    // Pipelines that only use a few significant bits of the float should be unaffected
    Buffer<float> im_ref = f.realize({kSize}, target);
    Buffer<float> im_fuzzed = f.realize({kSize}, target_fuzzed);

    for (int i = 0; i < im_ref.width(); i++) {
        // Test for exact floating point equality, which is exactly
        // the sort of thing FuzzFloatStores is trying to discourage.
        EXPECT_EQ(im_ref(i), im_fuzzed(i)) << "Expected exact floating point equality";
    }
}

TEST(FuzzFloatStoresTest, Affected) {
    // Check some code that should be affected
    Func f;
    Var x;
    f(x) = sqrt(x - 42.3333333f) / 17.0f - tan(x);
    f.vectorize(x, 8);

    Buffer<float> im_ref = f.realize({kSize}, target);
    Buffer<float> im_fuzzed = f.realize({kSize}, target_fuzzed);

    int differences = 0;
    for (int i = 0; i < im_ref.width(); i++) {
        // Pipelines that use all the bits should be wrong about half the time
        if (im_ref(i) != im_fuzzed(i)) {
            differences++;
        }
    }

    EXPECT_GT(differences, 0) << "fuzzing float stores should have done something";
    EXPECT_LT(differences, kSize) << "fuzzing float stores should not have changed every store";
}
