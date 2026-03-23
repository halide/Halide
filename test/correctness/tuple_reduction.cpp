#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    if (1) {
        // Test a tuple reduction on the gpu
        Func f;
        Var x, y, xo, yo, xi, yi;

        f(x, y) = Tuple(x + y, x - y);

        // Updates to a reduction are atomic.
        f(x, y) = Tuple(f(x, y)[1] * 2, f(x, y)[0] * 2);
        // now equals ((x - y)*2, (x + y)*2)

        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
            f.update().gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        } else if (target.has_feature(Target::HVX)) {
            f.hexagon(y).vectorize(x, 32);
            f.update().hexagon(y).vectorize(x, 32);
        }

        Realization result = f.realize({1024, 1024});

        Buffer<int> a = result[0], b = result[1];

        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = (x - y) * 2;
                int correct_b = (x + y) * 2;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return 1;
                }
            }
        }
    }

    if (1) {
        // Now test one that alternates between cpu and gpu per update step
        Func f;
        Var x, y, xo, yo, xi, yi;

        f(x, y) = Tuple(x + y, x - y);

        for (size_t i = 0; i < 10; i++) {
            // Swap the tuple elements and increment both
            f(x, y) = Tuple(f(x, y)[1] + 1, f(x, y)[0] + 1);
        }

        // Schedule the pure step and the odd update steps on the gpu
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        } else if (target.has_feature(Target::HVX)) {
            f.hexagon(y).vectorize(x, 32);
        }
        for (int i = 0; i < 10; i++) {
            f.update(i).unscheduled();
            if (i & 1) {
                if (target.has_gpu_feature()) {
                    f.update(i).gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
                } else if (target.has_feature(Target::HVX)) {
                    f.update(i).hexagon(y).vectorize(x, 32);
                }
            }
        }

        Realization result = f.realize({1024, 1024});

        Buffer<int> a = result[0], b = result[1];

        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = (x + y) + 10;
                int correct_b = (x - y) + 10;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return 1;
                }
            }
        }
    }

    if (1) {
        // Same as above, but switches which steps are gpu and cpu
        Func f;
        Var x, y, xo, yo, xi, yi;

        f(x, y) = Tuple(x + y, x - y);

        for (size_t i = 0; i < 10; i++) {
            // Swap the tuple elements and increment both
            f(x, y) = Tuple(f(x, y)[1] + 1, f(x, y)[0] + 1);
        }

        // Schedule the even update steps on the gpu
        for (int i = 0; i < 10; i++) {
            f.update(i).unscheduled();
            if (i & 1) {
                if (target.has_gpu_feature()) {
                    f.update(i).gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
                } else if (target.has_feature(Target::HVX)) {
                    f.update(i).hexagon(y).vectorize(x, 32);
                }
            }
        }

        Realization result = f.realize({1024, 1024});

        Buffer<int> a = result[0], b = result[1];

        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = (x + y) + 10;
                int correct_b = (x - y) + 10;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return 1;
                }
            }
        }
    }

    if (1) {
        // In this one, each step only uses one of the tuple elements
        // of the previous step, so only that buffer should get copied
        // back to host or copied to device.
        Func f;
        Var x, y, xo, yo, xi, yi;

        f(x, y) = Tuple(x + y - 1000, x - y + 1000);

        for (size_t i = 0; i < 10; i++) {
            f(x, y) = Tuple(f(x, y)[1] - 1, f(x, y)[1] + 1);
        }

        // Schedule the even update steps on the gpu
        for (int i = 0; i < 10; i++) {
            f.update(i).unscheduled();
            if ((i & 1) == 0) {
                if (target.has_gpu_feature()) {
                    f.update(i).gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
                } else if (target.has_feature(Target::HVX)) {
                    f.update(i).hexagon(y).vectorize(x, 32);
                }
            }
        }

        Realization result = f.realize({1024, 1024});

        Buffer<int> a = result[0], b = result[1];

        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = (x - y + 1000) + 8;
                int correct_b = (x - y + 1000) + 10;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return 1;
                }
            }
        }
    }

    {
        // A case which requires tuple updates to be atomic, but hides a
        // dependence in a way that triggered a bug in the past.
        Func f, g;
        Var x, y;

        f(x) = Tuple(x + 17, x + 1);
        constexpr int w = 100;

        RDom r(0, w);
        f(r) = Tuple(f(r)[0] + 5, f(clamp(f(r)[0], 0, w - 1))[1]);
        g(x, y) = mux(y, {f(x)[0], f(x)[1]});

        f.compute_root();

        Buffer<int> buf = g.realize({w, 2});
        Buffer<int> correct(w, 2);
        for (int x = 0; x < w; x++) {
            correct(x, 0) = x + 17;
            correct(x, 1) = x + 1;
        }
        for (int r = 0; r < w; r++) {
            int new_0 = correct(r, 0) + 5;
            int new_1 = correct(std::min(std::max(correct(r, 0), 0), w - 1), 1);
            // Tuple element 1 might depend on the old value of tuple element
            // zero. The new values must be both computed *then* assigned.
            correct(r, 0) = new_0;
            correct(r, 1) = new_1;
        }

        for (int x = 0; x < w; x++) {
            for (int y = 0; y < 2; y++) {
                if (buf(x, y) != correct(x, y)) {
                    printf("buf(%d, %d) = %d instead of %d\n", x, y, buf(x, y), correct(x, y));
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
