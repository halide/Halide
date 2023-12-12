#include "Halide.h"

using namespace Halide;

int run_test(Target t) {
    // Sliding window with the producer on the GPU and the consumer on
    // the CPU. This requires a copy inside the loop over which we are
    // sliding. Currently this copies the entire buffer back and
    // forth, which is suboptimal in the general case. In this
    // specific case we're folded over y, so copying the entire buffer
    // is not much more than just copying the part that was modified.

    Func f0{"f0_on_cpu"}, f1{"f1_on_gpu"}, f2{"f2_on_cpu"};
    Var x, y, tx, ty;

    // Produce something on CPU
    f0(x, y) = x + y;
    f0.compute_root();

    // Which we use to produce something on GPU, causing a copy_to_device.
    f1(x, y) = f0(x, y) + f0(x, y + 1);
    f1.compute_root().gpu_tile(x, y, tx, ty, 8, 8);

    // Which in turn we use to produce something on CPU, causing a copy_to_host.
    f2(x, y) = f1(x, y) * 2;
    f2.compute_root();

    // Make the buffer a little bigger so we actually can see the copy time.
    Buffer<int> out = f2.realize({2000, 2000}, t);
    // Let's only verify a part of it...
    for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
            int correct = 4 * (x + y) + 2;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                return 1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        printf("[SKIP] no gpu feature enabled\n");
        return 0;
    }
    printf("Testing without profiler.\n");
    int result = run_test(t);
    if (result != 0) {
        return 1;
    }

    printf("Testing thread based profiler.\n");
    result = run_test(t.with_feature(Target::Profile));
    if (result != 0) {
        return 1;
    }
    if (t.os == Target::Linux) {
        printf("Testing timer based profiler.\n");
        result = run_test(t.with_feature(Target::ProfileByTimer));
        if (result != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
