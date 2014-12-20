#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("No gpu target enabled. Skipping test.\n");
        return 0;
    }

    if (1) {
        // Test a tuple reduction on the gpu
        Func f;
        Var x, y;

        f(x, y) = Tuple(x + y, x - y);

        // Updates to a reduction are atomic.
        f(x, y) = Tuple(f(x, y)[1]*2, f(x, y)[0]*2);
        // now equals ((x - y)*2, (x + y)*2)

        f.gpu_tile(x, y, 16, 16);
        f.update().gpu_tile(x, y, 16, 16);

        Realization result = f.realize(1024, 1024);

        Image<int> a = result[0], b = result[1];

        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = (x - y)*2;
                int correct_b = (x + y)*2;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return -1;
                }
            }
        }
    }

    if (1) {
        // Now test one that alternates between cpu and gpu per update step
        Func f;
        Var x, y;

        f(x, y) = Tuple(x + y, x - y);

        for (size_t i = 0; i < 10; i++) {
            // Swap the tuple elements and increment both
            f(x, y) = Tuple(f(x, y)[1] + 1, f(x, y)[0] + 1);
        }

        // Schedule the pure step and the odd update steps on the gpu
        f.gpu_tile(x, y, 16, 16);
        for (int i = 0; i < 10; i ++) {
	    if (i & 1) {
		f.update(i).gpu_tile(x, y, 16, 16);
	    } else {
		f.update(i);
	    }
        }

        Realization result = f.realize(1024, 1024);

        Image<int> a = result[0], b = result[1];

        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = (x + y) + 10;
                int correct_b = (x - y) + 10;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return -1;
                }
            }
        }

    }

    if (1) {
        // Same as above, but switches which steps are gpu and cpu
        Func f;
        Var x, y;

        f(x, y) = Tuple(x + y, x - y);

        for (size_t i = 0; i < 10; i++) {
            // Swap the tuple elements and increment both
            f(x, y) = Tuple(f(x, y)[1] + 1, f(x, y)[0] + 1);
        }

        // Schedule the even update steps on the gpu
        for (int i = 0; i < 10; i ++) {
            if (i & 1) {
                f.update(i).gpu_tile(x, y, 16, 16);
            } else {
                f.update(i);
            }
        }

        Realization result = f.realize(1024, 1024);

        Image<int> a = result[0], b = result[1];

        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = (x + y) + 10;
                int correct_b = (x - y) + 10;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return -1;
                }
            }
        }

    }

    if (1) {
        // In this one, each step only uses one of the tuple elements
        // of the previous step, so only that buffer should get copied
        // back to host or copied to device.
        Func f;
        Var x, y;

        f(x, y) = Tuple(x + y - 1000, x - y + 1000);

        for (size_t i = 0; i < 10; i++) {
            f(x, y) = Tuple(f(x, y)[1] - 1, f(x, y)[1] + 1);
        }

        // Schedule the even update steps on the gpu
        for (int i = 0; i < 10; i++) {
            if (i & 1) {
                f.update(i);
            } else {
                f.update(i).gpu_tile(x, y, 16, 16);
            }
        }

        Realization result = f.realize(1024, 1024);

        Image<int> a = result[0], b = result[1];

        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = (x - y + 1000) + 8;
                int correct_b = (x - y + 1000) + 10;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return -1;
                }
            }
        }

    }

    return 0;
}
