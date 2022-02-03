#include "Halide.h"

namespace {

using namespace Halide;

Expr sum3x3(Func f, Var x, Var y) {
    return f(x - 1, y - 1) + f(x - 1, y) + f(x - 1, y + 1) +
           f(x, y - 1) + f(x, y) + f(x, y + 1) +
           f(x + 1, y - 1) + f(x + 1, y) + f(x + 1, y + 1);
}

class Harris : public Halide::Generator<Harris> {
public:
    Input<Buffer<float, 3>> input{"input"};
    Output<Buffer<float, 2>> output{"output"};

    void generate() {
        Var x("x"), y("y"), c("c");

        // Algorithm
        Func gray("gray");
        gray(x, y) = (0.299f * input(x, y, 0) +
                      0.587f * input(x, y, 1) +
                      0.114f * input(x, y, 2));

        Func Iy("Iy");
        Iy(x, y) = gray(x - 1, y - 1) * (-1.0f / 12) + gray(x - 1, y + 1) * (1.0f / 12) +
                   gray(x, y - 1) * (-2.0f / 12) + gray(x, y + 1) * (2.0f / 12) +
                   gray(x + 1, y - 1) * (-1.0f / 12) + gray(x + 1, y + 1) * (1.0f / 12);

        Func Ix("Ix");
        Ix(x, y) = gray(x - 1, y - 1) * (-1.0f / 12) + gray(x + 1, y - 1) * (1.0f / 12) +
                   gray(x - 1, y) * (-2.0f / 12) + gray(x + 1, y) * (2.0f / 12) +
                   gray(x - 1, y + 1) * (-1.0f / 12) + gray(x + 1, y + 1) * (1.0f / 12);

        Func Ixx("Ixx");
        Ixx(x, y) = Ix(x, y) * Ix(x, y);

        Func Iyy("Iyy");
        Iyy(x, y) = Iy(x, y) * Iy(x, y);

        Func Ixy("Ixy");
        Ixy(x, y) = Ix(x, y) * Iy(x, y);

        Func Sxx("Sxx");
        Sxx(x, y) = sum3x3(Ixx, x, y);

        Func Syy("Syy");
        Syy(x, y) = sum3x3(Iyy, x, y);

        Func Sxy("Sxy");
        Sxy(x, y) = sum3x3(Ixy, x, y);

        Func det("det");
        det(x, y) = Sxx(x, y) * Syy(x, y) - Sxy(x, y) * Sxy(x, y);

        Func trace("trace");
        trace(x, y) = Sxx(x, y) + Syy(x, y);

        output(x, y) = det(x, y) - 0.04f * trace(x, y) * trace(x, y);

        // Estimates (for autoscheduler; ignored otherwise)
        {
            const int kWidth = 1536;
            const int kHeight = 2560;
            input.dim(0).set_estimate(0, kWidth);
            input.dim(1).set_estimate(0, kHeight);
            input.dim(2).set_estimate(0, 3);
            output.dim(0).set_estimate(3, kWidth - 6);
            output.dim(1).set_estimate(3, kHeight - 6);
        }

        // Schedule
        if (!auto_schedule) {
            Var xi("xi"), yi("yi");
            if (get_target().has_gpu_feature()) {
                // 0.253ms on a 2060 RTX
                output.gpu_tile(x, y, xi, yi, 62, 14)
                    .unroll(xi, 2);
                gray.compute_at(output, x)
                    .gpu_threads(x, y)
                    .tile(x, y, xi, yi, 3, 2)
                    .unroll(xi)
                    .unroll(yi);
                gray.in()
                    .compute_at(Iy, x)
                    .vectorize(x, 2)
                    .unroll(x)
                    .unroll(y);
                Ix.compute_at(output, x)
                    .gpu_threads(x, y)
                    .unroll(x, 2);
                Iy.compute_at(output, x)
                    .gpu_threads(x, y)
                    .unroll(x, 2);
                Ix.compute_with(Iy, x);
                Ix.in()
                    .compute_at(output, xi)
                    .vectorize(x, 2)
                    .unroll(x)
                    .unroll(y);
                Iy.in()
                    .compute_at(output, xi)
                    .vectorize(x, 2)
                    .unroll(x)
                    .unroll(y);
            } else {
                // 0.92ms on an Intel i9-9960X using 16 threads
                const int vec = natural_vector_size<float>();
                output.split(y, y, yi, 32)
                    .parallel(y)
                    .vectorize(x, vec);
                gray.store_at(output, y)
                    .compute_at(output, yi)
                    .vectorize(x, vec);
                Ix.store_at(output, y)
                    .compute_at(output, yi)
                    .vectorize(x, vec);
                Iy.store_at(output, y)
                    .compute_at(output, yi)
                    .vectorize(x, vec);
                Ix.compute_with(Iy, x);
            }
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Harris, harris)
