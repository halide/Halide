#include "Halide.h"

#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    struct Prec {
        Halide::ApproximationPrecision precision;
        float epsilon;
    } precisions_to_test[] = {
        {Halide::MAE_1e_2, 1e-2f},
        {Halide::MAE_1e_3, 1e-3f},
        {Halide::MAE_1e_4, 1e-4f},
        {Halide::MAE_1e_5, 1e-5f},
        {Halide::MAE_1e_6, 1e-6f}};

    for (Prec precision : precisions_to_test) {
        fprintf(stderr, "\nTesting for precision %e...\n", precision.epsilon);
        Func atan_f, atan2_f;
        Var x, y;
        const int steps = 1000;
        Expr vx = (x - steps / 2) / float(steps);
        Expr vy = (y - steps / 2) / float(steps);

        atan_f(x) = fast_atan(vx, precision.precision);
        if (target.has_gpu_feature()) {
            Var xo, xi;
            Var yo, yi;
            atan_f.never_partition_all();
            atan_f.gpu_tile(x, xo, xi, 256, TailStrategy::ShiftInwards);
        } else {
            atan_f.vectorize(x, 8);
        }

        fprintf(stderr, "    Testing fast_atan() correctness...  ");
        Buffer<float> atan_result = atan_f.realize({steps});
        float max_error = 0.0f;
        for (int i = 0; i < steps; ++i) {
            const float x = (i - steps / 2) / float(steps);
            const float atan_x = atan_result(i);
            const float atan_x_ref = atan(x);
            float abs_error = std::abs(atan_x_ref - atan_x);
            max_error = std::max(max_error, abs_error);
            if (abs_error > precision.epsilon) {
                fprintf(stderr, "fast_atan(%.6f) = %.20f not equal to %.20f (error=%.5e)\n", x, atan_x, atan_x_ref, atan_x_ref - atan_x);
                exit(1);
            }
        }
        fprintf(stderr, "Passed: max abs error: %.5e\n", max_error);

        atan2_f(x, y) = fast_atan2(vx, vy, precision.precision);
        if (target.has_gpu_feature()) {
            Var xo, xi;
            Var yo, yi;
            atan2_f.never_partition_all();
            atan2_f.gpu_tile(x, y, xo, yo, xi, yi, 32, 8, TailStrategy::ShiftInwards);
        } else {
            atan2_f.vectorize(x, 8);
        }
        fprintf(stderr, "    Testing fast_atan2() correctness...  ");
        Buffer<float> atan2_result = atan2_f.realize({steps, steps});
        max_error = 0.0f;
        for (int i = 0; i < steps; ++i) {
            const float x = (i - steps / 2) / float(steps);
            for (int j = 0; j < steps; ++j) {
                const float y = (j - steps / 2) / float(steps);
                const float atan2_x_y = atan2_result(i, j);
                const float atan2_x_y_ref = atan2(x, y);
                float abs_error = std::abs(atan2_x_y_ref - atan2_x_y);
                max_error = std::max(max_error, abs_error);
                if (abs_error > precision.epsilon) {
                    fprintf(stderr, "fast_atan2(%.6f, %.6f) = %.20f not equal to %.20f (error=%.5e)\n", x, y, atan2_x_y, atan2_x_y_ref, atan2_x_y_ref - atan2_x_y);
                    exit(1);
                }
            }
        }
        fprintf(stderr, "Passed: max abs error: %.5e\n", max_error);
    }

    printf("Success!\n");
    return 0;
}
