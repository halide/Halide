#include "Halide.h"

#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif

using namespace Halide;

int main(int argc, char **argv) {
    Func atan_f, atan2_f;
    Var x, y;
    const int steps = 1000;
    Expr vx = (x - steps / 2) / float(steps);
    Expr vy = (y - steps / 2) / float(steps);

    atan_f(x) = fast_atan(vx, Halide::ApproximationPrecision::MAE_1e_5);
    atan_f.vectorize(x, 8);

    fprintf(stderr, "Testing fast_atan() correctness...\n");
    Buffer<float> atan_result = atan_f.realize({steps});
    float max_error = 0.0f;
    for (int i = 0; i < steps; ++i) {
        const float x = (i - steps / 2) / float(steps);
        const float atan_x = atan_result(i);
        const float atan_x_ref = atan(x);
        float abs_error = std::abs(atan_x_ref - atan_x);
        max_error = std::max(max_error, abs_error);
        if (abs_error > 1e-5f) {
            fprintf(stderr, "fast_atan(%.6f) = %.20f not equal to %.20f (error=%.20f)\n", x, atan_x, atan_x_ref, atan_x_ref - atan_x);
            exit(1);
        }
    }
    fprintf(stderr, "Passed: max abs error: %.5e\n", max_error);

    atan2_f(x, y) = fast_atan2(vx, vy,
                               Halide::ApproximationPrecision::MAE_1e_5);
    atan2_f.vectorize(x, 8);
    std::printf("Testing fast_atan2() correctness...\n");
    Buffer<float> atan2_result = atan2_f.realize({steps, steps});
    max_error = 0.0f;
    for (int i = 0; i < steps; ++i) {
        const float x = (i - steps / 2) / float(steps);
        for (int j = 0; j < steps; ++j) {
            const float y = (j - steps / 2) / float(steps);
            if (x == 0.0f && y == 0.0f) {
                continue;
            }
            const float atan2_x_y = atan2_result(i, j);
            const float atan2_x_y_ref = atan2(x, y);
            float abs_error = std::abs(atan2_x_y_ref - atan2_x_y);
            max_error = std::max(max_error, abs_error);
            if (abs_error > 1e-5) {
                fprintf(stderr, "fast_atan2(%.6f, %.6f) = %.20f not equal to %.20f (error=%.20f)\n", x, y, atan2_x_y, atan2_x_y_ref, atan2_x_y_ref - atan2_x_y);
                exit(1);
            }
        }
    }
    fprintf(stderr, "Passed: max abs error: %.5e\n", max_error);

    printf("Success!\n");
    return 0;
}
