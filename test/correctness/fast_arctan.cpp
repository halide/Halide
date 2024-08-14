#include "Halide.h"

using namespace Halide;

int bits_diff(float fa, float fb) {
    uint32_t a = Halide::Internal::reinterpret_bits<uint32_t>(fa);
    uint32_t b = Halide::Internal::reinterpret_bits<uint32_t>(fb);
    uint32_t a_exp = a >> 23;
    uint32_t b_exp = b >> 23;
    if (a_exp != b_exp) return -100;
    uint32_t diff = a > b ? a - b : b - a;
    int count = 0;
    while (diff) {
        count++;
        diff /= 2;
    }
    return count;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    struct Prec {
        ApproximationPrecision precision;
        float epsilon;
        const char *objective;
    } precisions_to_test[] = {
        // MAE
        {ApproximationPrecision::MAE_1e_2, 1e-2f, "MAE"},
        {ApproximationPrecision::MAE_1e_3, 1e-3f, "MAE"},
        {ApproximationPrecision::MAE_1e_4, 1e-4f, "MAE"},
        {ApproximationPrecision::MAE_1e_5, 1e-5f, "MAE"},
        {ApproximationPrecision::MAE_1e_6, 1e-6f, "MAE"},

        // MULPE
        {ApproximationPrecision::MULPE_1e_2, 1e-2f, "MULPE"},
        {ApproximationPrecision::MULPE_1e_3, 1e-3f, "MULPE"},
        {ApproximationPrecision::MULPE_1e_4, 1e-4f, "MULPE"},
        {ApproximationPrecision::MULPE_1e_5, 1e-5f, "MULPE"},
        {ApproximationPrecision::MULPE_1e_6, 1e-6f, "MULPE"},
    };

    for (Prec precision : precisions_to_test) {
        printf("\nTesting for precision %.1e (%s optimized)...\n", precision.epsilon, precision.objective);
        Func atan_f, atan2_f;
        Var x, y;
        const int steps = 1000;
        Expr vx = (x - steps / 2) / float(steps / 8);
        Expr vy = (y - steps / 2) / float(steps / 8);

        atan_f(x) = fast_atan(vx, precision.precision);
        if (target.has_gpu_feature()) {
            Var xo, xi;
            Var yo, yi;
            atan_f.never_partition_all();
            atan_f.gpu_tile(x, xo, xi, 256, TailStrategy::ShiftInwards);
        } else {
            atan_f.vectorize(x, 8);
        }

        printf("    Testing fast_atan() correctness...  ");
        Buffer<float> atan_result = atan_f.realize({steps});
        float max_error = 0.0f;
        int max_mantissa_error = 0;
        for (int i = 0; i < steps; ++i) {
            const float x = (i - steps / 2) / float(steps / 8);
            const float atan_x = atan_result(i);
            const float atan_x_ref = atan(x);
            float abs_error = std::abs(atan_x_ref - atan_x);
            int mantissa_error = bits_diff(atan_x, atan_x_ref);
            max_error = std::max(max_error, abs_error);
            max_mantissa_error = std::max(max_mantissa_error, mantissa_error);
            if (abs_error > precision.epsilon) {
                fprintf(stderr, "fast_atan(%.6f) = %.20f not equal to %.20f (error=%.5e)\n", x, atan_x, atan_x_ref, atan_x_ref - atan_x);
                exit(1);
            }
        }
        printf("Passed: max abs error: %.5e  max mantissa bits wrong: %d\n", max_error, max_mantissa_error);

        atan2_f(x, y) = fast_atan2(vx, vy, precision.precision);
        if (target.has_gpu_feature()) {
            Var xo, xi;
            Var yo, yi;
            atan2_f.never_partition_all();
            atan2_f.gpu_tile(x, y, xo, yo, xi, yi, 32, 8, TailStrategy::ShiftInwards);
        } else {
            atan2_f.vectorize(x, 8);
        }
        printf("    Testing fast_atan2() correctness...  ");
        Buffer<float> atan2_result = atan2_f.realize({steps, steps});
        max_error = 0.0f;
        max_mantissa_error = 0;
        for (int i = 0; i < steps; ++i) {
            const float x = (i - steps / 2) / float(steps / 8);
            for (int j = 0; j < steps; ++j) {
                const float y = (j - steps / 2) / float(steps / 8);
                const float atan2_x_y = atan2_result(i, j);
                const float atan2_x_y_ref = atan2(x, y);
                float abs_error = std::abs(atan2_x_y_ref - atan2_x_y);
                int mantissa_error = bits_diff(atan2_x_y, atan2_x_y_ref);
                max_error = std::max(max_error, abs_error);
                max_mantissa_error = std::max(max_mantissa_error, mantissa_error);
                if (abs_error > precision.epsilon) {
                    fprintf(stderr, "fast_atan2(%.6f, %.6f) = %.20f not equal to %.20f (error=%.5e)\n", x, y, atan2_x_y, atan2_x_y_ref, atan2_x_y_ref - atan2_x_y);
                    exit(1);
                }
            }
        }
        printf("Passed: max abs error: %.5e  max mantissa bits wrong: %d\n", max_error, max_mantissa_error);
    }

    printf("Success!\n");
    return 0;
}
