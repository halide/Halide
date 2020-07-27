#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    t.set_feature(Target::DisableLLVMLoopOpt);

    Func f1, f2;
    Var x, y;
    RDom r1(0, 100000), r2(0, 100000, 0, 128);
    Param<int> stride;
    stride.set(1);

    Func input;
    input(x) = cast<float>(x);

    f1(x, y) = 0.0f;
    f1(x, y) += input(x + r1 + y);

    f2(x, y) = 0.0f;
    f2(x, r2.y * stride) += input(x + r2.x + r2.y * stride);

    RVar r2_yi, r2_yii;
    Var xi, yi, yii;
    if (t.has_gpu_feature()) {

        input.compute_root()
            .gpu_tile(x, xi, 32);

        f1.compute_root()
            .gpu_tile(x, y, xi, yi, 32, 8)
            .update()
            .gpu_tile(x, y, xi, yi, 32, 32)
            .split(yi, yi, yii, 4)
            .unroll(yii)
            .reorder(yii, r1, xi, yi, x, y);

        f2.compute_root()
            .gpu_tile(x, y, xi, yi, 32, 8)
            .update()
            .allow_race_conditions()  // So we can parallelize r2.y
            .gpu_tile(x, r2.y, xi, r2_yi, 32, 32)
            .split(r2_yi, r2_yi, r2_yii, 4)
            .unroll(r2_yii)
            .reorder(r2_yii, r2.x, xi, r2_yi, x, r2.y);

    } else {
        // CPU schedule

        input.compute_root()
            .vectorize(x, 8);

        f1.compute_root()
            .vectorize(x, 8)
            .update()
            .tile(x, y, xi, yi, 8, 4)
            .reorder(xi, yi, r1, x, y)
            .vectorize(xi)
            .unroll(yi);

        f2.compute_root()
            .vectorize(x, 8)
            .update()
            .tile(x, r2.y, xi, r2_yi, 8, 4)
            .reorder(xi, r2_yi, r2.x, x, r2.y)
            .vectorize(xi)
            .unroll(r2_yi);
    }

    // With stride set to 1, f1 and f2 are functionally the
    // same. There's an important difference in performance though. In
    // f1's update definition, the y variable is pure, which means the
    // distinct values of y in the unrolled block can be computed
    // separately (even though the stride in y is unknown). This means
    // the summation can be done in a register.

    // In f2's update definition, it's unknown at compile time whether
    // or not the distinct references to f2 alias, so the inner loop
    // must do a full read-modify-write to a memory location for each
    // access to f2.

    Buffer<float> out(128, 128);

    double t1 = Tools::benchmark([&]() {
        f1.realize(out, t);
        out.device_sync();
    });

    double t2 = Tools::benchmark([&]() {
        f2.realize(out, t);
        out.device_sync();
    });

    printf("Unrolled pure var: %f ms\n"
           "Unrolled rvar: %f ms\n",
           t1 * 1000,
           t2 * 1000);

    // f1 should be about 3x faster than f2
    if (t1 >= t2) {
        printf("The unrolled pure var should have been faster than the unrolled rvar\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
