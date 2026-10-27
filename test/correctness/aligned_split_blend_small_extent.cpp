#include "Halide.h"
#include <stdio.h>

// Blend tail strategies of an aligned split of an update definition, over
// every small required region and anchor phase.
//
// When the required extent n is smaller than the split factor k, the
// required region can straddle a line of the anchored grid. Then two
// tiles cover it, and both are clamped by the same pair of bounds
// (low_bound > high_bound). A ShiftInwardsAndBlend mask computed from only
// one of the two clamps drops required elements. For example, with k = 8,
// anchor 0 and region [5, 10], elements 6 and 7 were never updated.

using namespace Halide;

int test(bool gpu) {
    const int k = 8;
    for (auto ts : {TailStrategy::ShiftInwardsAndBlend, TailStrategy::RoundUpAndBlend}) {
        for (bool vec : {false, true}) {
            Var x{"x"}, xo{"xo"}, xi{"xi"};
            Func f{"f"};
            Param<int> anchor{"anchor"};

            f(x) = 0;
            f(x) = f(x) + 1;
            f.compute_root();
            Stage s = f.update().aligned_split(x, xo, xi, k, anchor, ts);
            if (vec && gpu) {
                // Not every GPU backend has vectors of k lanes (HLSL needs
                // sm69 for more than 4), so vectorize the tile in pieces of 4.
                s.vectorize(xi, 4);
            } else if (vec) {
                s.vectorize(xi);
            }
            if (gpu) {
                // The blend reads and writes neighboring tiles, so the tiles
                // can't run in parallel.
                s.gpu_single_thread();
            }

            // Read f through another Func so the blend writes into an
            // internal, paddable allocation.
            Func out{"out"};
            out(x) = f(x);
            out.compile_jit();

            for (int a = 0; a < k; a++) {
                anchor.set(a);
                for (int m = -k - 1; m <= k + 1; m++) {
                    for (int n = 1; n <= 2 * k + 3; n++) {
                        Buffer<int> im(n);
                        im.set_min(m);
                        out.realize(im);
                        for (int i = m; i < m + n; i++) {
                            if (im(i) != 1) {
                                printf("f(%d) = %d instead of 1 (ts: %d, vectorized: %d, "
                                       "k: %d, anchor: %d, region: [%d, %d])\n",
                                       i, im(i), (int)ts, (int)vec, k, a, m, m + n - 1);
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (test(false) != 0) {
        return 1;
    }
    if (get_jit_target_from_environment().has_gpu_feature()) {
        printf("Testing the GPU schedule\n");
        if (test(true) != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
