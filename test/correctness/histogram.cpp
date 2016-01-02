#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    int W = 128, H = 128;

    // Compute a random image and its true histogram
    int reference_hist[256];
    for (int i = 0; i < 256; i++) {
        reference_hist[i] = 0;
    }

    Image<float> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = float(rand() & 0x000000ff);
            reference_hist[uint8_t(in(x, y))] += 1;
        }
    }

    Func hist("hist");
    Var x;

    RDom r(in);
    hist(x) = 0;
    hist(clamp(cast<int>(in(r.x, r.y)), 0, 255)) += 1;

    hist.compute_root();

    Func g;

    g(x) = hist(x+10);

    // No parallel reductions
    /*
    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
	hist.gpu_tile(x, 64);
	hist.update().gpu_tile(r.x, r.y, 16, 16);
    }
    */

    Image<int32_t> histogram = g.realize(10); // buckets 10-20 only

    for (int i = 10; i < 20; i++) {
        if (histogram(i-10) != reference_hist[i]) {
            printf("Error: bucket %d is %d instead of %d\n", i, histogram(i), reference_hist[i]);
            return -1;
        }
    }

    printf("Success!\n");

    return 0;

}
