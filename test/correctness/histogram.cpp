#include "Halide.h"
#include <stdio.h>

using namespace Halide;

template<typename ITYPE, typename HTYPE>
bool test() {
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
    g(x) = hist(x+10);

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
    if (target.features_any_of({Target::HVX_64, Target::HVX})) {
        const int vector_size = target.has_feature(Target::HVX) ? 128 : 64;
        g
            .hexagon()
            .vectorize(x, vector_size);

        hist
            .compute_at(g, Var::outermost())
            .vectorize(x, vector_size);

        if (target.has_feature(Target::HVX_v65)) {
            hist.store_in(MemoryType::VTCM);

            hist
                .update(0)
                .allow_race_conditions()
                .vectorize(r.x, vector_size);
        }
    } else {
        hist.compute_root();
    }

    Buffer<HTYPE> histogram = g.realize(128); // buckets 10-137

    for (int i = 10; i < 138; i++) {
        if (histogram(i-10) != reference_hist[i]) {
            printf("Error: bucket %d is %d instead of %d\n", i, histogram(i-10), reference_hist[i]);
            return false;
        }
    }

    return true;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    if (target.features_any_of({Target::HVX_64, Target::HVX})) {
        if (!test<uint8_t,  int16_t >() ||
            !test<uint16_t, uint16_t>() ||
            !test<uint8_t,  int32_t >() ||
            !test<uint32_t, uint32_t>()) return 1;
    } else {
        if (!test<float, int>()) return 1;
    }

    printf("Success!\n");
    return 0;
}
