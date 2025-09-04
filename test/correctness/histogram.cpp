#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
template<typename ITYPE, typename HTYPE>
void test_histogram() {
    int W = 128, H = 128;

    // Compute a random image and its true histogram
    HTYPE reference_hist[256];
    for (int i = 0; i < 256; i++) {
        reference_hist[i] = HTYPE(0);
    }

    Buffer<ITYPE> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = ITYPE(rand() & 0x000000ff);
            reference_hist[uint8_t(in(x, y))] += HTYPE(1);
        }
    }

    Func hist("hist"), g("g");
    Var x;

    RDom r(in);
    hist(x) = cast<HTYPE>(0);
    hist(clamp(cast<int>(in(r.x, r.y)), 0, 255)) += cast<HTYPE>(1);
    g(x) = hist(x + 10);

    // No parallel reductions
    /*
    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xi;
        hist.gpu_tile(x, xi, 64);
        RVar rxi, ryi;
        hist.update().gpu_tile(r.x, r.y, rxi, ryi, 16, 16);
    }
    */
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::HVX)) {
        const int vector_size = target.has_feature(Target::HVX) ? 128 : 64;
        g
            .hexagon()
            .vectorize(x, vector_size);

        hist
            .compute_at(g, Var::outermost())
            .vectorize(x, vector_size);

        if (target.features_any_of({Target::HVX_v65, Target::HVX_v66,
                                    Target::HVX_v68})) {
            hist.store_in(MemoryType::VTCM);

            hist
                .update(0)
                .allow_race_conditions()
                .vectorize(r.x, vector_size);
        }
    } else {
        hist.compute_root();
    }

    Buffer<HTYPE> histogram = g.realize({128});  // buckets 10-137

    for (int i = 10; i < 138; i++) {
        EXPECT_EQ(histogram(i - 10), reference_hist[i]) << "bucket " << i;
    }
}
}  // namespace

TEST(HistogramTest, HVX) {
    if (!get_jit_target_from_environment().has_feature(Target::HVX)) {
        GTEST_SKIP() << "HVX not available";
    }
    test_histogram<uint8_t, int16_t>();
    test_histogram<uint16_t, uint16_t>();
    test_histogram<uint8_t, int32_t>();
    test_histogram<uint32_t, uint32_t>();
}

TEST(HistogramTest, Generic) {
    if (get_jit_target_from_environment().has_feature(Target::HVX)) {
        GTEST_SKIP() << "Skipping generic test when HVX is available";
    }
    test_histogram<float, int>();
}
