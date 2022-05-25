#include "Halide.h"

namespace {

class Unsharp : public Halide::Generator<Unsharp> {
public:
    GeneratorParam<float> sigma{"sigma", 1.5f};

    Input<Buffer<float, 3>> input{"input"};
    Output<Buffer<float, 3>> output{"output"};

    void generate() {
        Var x("x"), y("y"), c("c");

        const float kPi = 3.14159265358979310000f;

        Func kernel("kernel");
        kernel(x) = exp(-x * x / (2 * sigma * sigma)) / (sqrtf(2 * kPi) * sigma);

        Func input_bounded = Halide::BoundaryConditions::repeat_edge(input);

        Func gray("gray");
        gray(x, y) = (0.299f * input_bounded(x, y, 0) +
                      0.587f * input_bounded(x, y, 1) +
                      0.114f * input_bounded(x, y, 2));

        Func blur_y("blur_y");
        blur_y(x, y) = (kernel(0) * gray(x, y) +
                        kernel(1) * (gray(x, y - 1) +
                                     gray(x, y + 1)) +
                        kernel(2) * (gray(x, y - 2) +
                                     gray(x, y + 2)) +
                        kernel(3) * (gray(x, y - 3) +
                                     gray(x, y + 3)));

        Func blur_x("blur_x");
        blur_x(x, y) = (kernel(0) * blur_y(x, y) +
                        kernel(1) * (blur_y(x - 1, y) +
                                     blur_y(x + 1, y)) +
                        kernel(2) * (blur_y(x - 2, y) +
                                     blur_y(x + 2, y)) +
                        kernel(3) * (blur_y(x - 3, y) +
                                     blur_y(x + 3, y)));

        Func sharpen("sharpen");
        sharpen(x, y) = 2 * gray(x, y) - blur_x(x, y);

        Func ratio("ratio");
        ratio(x, y) = sharpen(x, y) / gray(x, y);

        output(x, y, c) = ratio(x, y) * input(x, y, c);

        // Estimates (for autoscheduler; ignored otherwise)
        {
            input.dim(0).set_estimate(0, 1536);
            input.dim(1).set_estimate(0, 2560);
            input.dim(2).set_estimate(0, 3);
            output.dim(0).set_estimate(0, 1536);
            output.dim(1).set_estimate(0, 2560);
            output.dim(2).set_estimate(0, 3);
        }

        // Schedule
        if (!auto_schedule) {
            // Some Intel Mac Minis have GPUs that require tile sizes smaller than 32x32
            // for this pipeline because they have too few registers. Drop to 16x16 to
            // avoid unexpected crashes in CI.
            const int tile_size = get_target().has_feature(Target::Metal) ? 16 : 32;

            if (get_target().has_gpu_feature()) {
                // The timing of this schedule is oddly noisy. Runs
                // from 0.4ms to 0.5ms on a 2060 RTX.  Oddly, the
                // better runtimes occur when running under nvprof.
                Var xi, yi;
                output.compute_root()
                    .reorder(c, x, y)
                    .gpu_tile(x, y, xi, yi, tile_size, tile_size)
                    .bound(c, 0, 3)
                    .unroll(c);
                ratio.compute_at(output, xi);
                gray.compute_at(output, x)
                    .tile(x, y, xi, yi, 2, 2)
                    .unroll(xi)
                    .unroll(yi)
                    .gpu_threads(x, y);
                blur_y.compute_at(output, x)
                    .unroll(x, 2)
                    .gpu_threads(x, y);
            } else {
                // 1.93ms on an Intel i9-9960X using 16 threads
                Var yo, yi;
                const int vec = natural_vector_size<float>();

                output.split(y, yo, yi, 32)
                    .vectorize(x, vec)
                    .parallel(yo)
                    .reorder(x, c, yi, yo);
                gray.compute_at(output, yi)
                    .store_at(output, yo)
                    .vectorize(x, vec);
                blur_y.compute_at(output, yi)
                    .store_at(output, yo)
                    .vectorize(x, vec);
                ratio.compute_at(output, yi)
                    .store_at(output, yo)
                    .vectorize(x, vec);
            }
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Unsharp, unsharp)
