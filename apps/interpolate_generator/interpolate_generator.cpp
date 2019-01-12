#include "Halide.h"
#include "../autoscheduler/SimpleAutoSchedule.h"

namespace {

class Interpolate : public Halide::Generator<Interpolate> {
public:
    GeneratorParam<int>     levels_{"levels", 10};

    Input<Buffer<float>>    input_{"input", 3};
    Output<Buffer<float>>   output_{"output", 3};

    void generate() {
        Var x("x"), y("y"), c("c");

        const int levels = levels_;

        // Input must have four color channels - rgba
        input_.dim(2).set_bounds(0, 4);

        Func downsampled[levels];
        Func downx[levels];
        Func interpolated[levels];
        Func upsampled[levels];
        Func upsampledx[levels];

        Func clamped = Halide::BoundaryConditions::repeat_edge(input_);

        downsampled[0](x, y, c) = select(c < 3, clamped(x, y, c) * clamped(x, y, 3), clamped(x, y, 3));

        for (int l = 1; l < levels; ++l) {
            Func prev = downsampled[l-1];

            if (l == 4) {
                // Also add a boundary condition at a middle pyramid level
                // to prevent the footprint of the downsamplings to extend
                // too far off the base image. Otherwise we look 512
                // pixels off each edge.
                Expr w = input_.width()/(1 << l);
                Expr h = input_.height()/(1 << l);
                prev = lambda(x, y, c, prev(clamp(x, 0, w), clamp(y, 0, h), c));
            }

            downx[l](x, y, c) = (prev(x*2-1, y, c) +
                                 2.0f * prev(x*2, y, c) +
                                 prev(x*2+1, y, c)) * 0.25f;
            downsampled[l](x, y, c) = (downx[l](x, y*2-1, c) +
                                       2.0f * downx[l](x, y*2, c) +
                                       downx[l](x, y*2+1, c)) * 0.25f;
        }
        interpolated[levels-1](x, y, c) = downsampled[levels-1](x, y, c);
        for (int l = levels-2; l >= 0; --l) {
            upsampledx[l](x, y, c) = (interpolated[l+1](x/2, y, c) +
                                      interpolated[l+1]((x+1)/2, y, c)) / 2.0f;
            upsampled[l](x, y, c) =  (upsampledx[l](x, y/2, c) +
                                      upsampledx[l](x, (y+1)/2, c)) / 2.0f;
            interpolated[l](x, y, c) = downsampled[l](x, y, c) + (1.0f - downsampled[l](x, y, 3)) * upsampled[l](x, y, c);
        }

        Func normalize("normalize");
        normalize(x, y, c) = interpolated[0](x, y, c) / interpolated[0](x, y, 3);

        // Schedule
        if (auto_schedule) {
            output_ = normalize;
        } else {
            Var yo, yi, xo, xi, ci;
            if (get_target().has_gpu_feature()) {
                std::string use_simple_autoscheduler =
                    Halide::Internal::get_env_variable("HL_USE_SIMPLE_AUTOSCHEDULER");
                if (use_simple_autoscheduler == "1") {
                    output_ = normalize;
                    Halide::SimpleAutoscheduleOptions options;
                    options.gpu = get_target().has_gpu_feature();
                    options.gpu_tile_channel = 1;
                    Func output_func = output_;
                    Halide::simple_autoschedule(output_func,
                            {{"input.min.0", 0},
                            {"input.extent.0", 1536},
                            {"input.min.1", 0},
                            {"input.extent.1", 2560},
                            {"input.min.2", 0},
                            {"input.extent.2", 4}},
                            {{0, 1536},
                             {0, 2560},
                             {0, 3}},
                            options);
                } else {
                    // Some gpus don't have enough memory to process the entire
                    // image, so we process the image in tiles.

                    // We can't compute the entire output stage at once on the GPU
                    // - it takes too much GPU memory on some of our build bots,
                    // so we wrap the final stage in a CPU stage.
                    Func cpu_wrapper = normalize.in();

                    cpu_wrapper
                        .reorder(c, x, y)
                        .bound(c, 0, 3)
                        .tile(x, y, xo, yo, xi, yi, input_.width()/4, input_.height()/4)
                        .vectorize(xi, 8);

                    normalize
                        .compute_at(cpu_wrapper, xo)
                        .reorder(c, x, y)
                        .gpu_tile(x, y, xi, yi, 16, 16)
                        .unroll(c);

                    // Start from level 1 to save memory - level zero will be computed on demand
                    for (int l = 1; l < levels; ++l) {
                        int tile_size = 32 >> l;
                        if (tile_size < 1) tile_size = 1;
                        if (tile_size > 8) tile_size = 8;
                        downsampled[l]
                            .compute_root()
                            .gpu_tile(x, y, c, xi, yi, ci, tile_size, tile_size, 4);
                        if (l == 1 || l == 4) {
                            interpolated[l]
                                .compute_at(cpu_wrapper, xo)
                                .gpu_tile(x, y, c, xi, yi, ci, 8, 8, 4);
                        } else {
                            int parent = l > 4 ? 4 : 1;
                            interpolated[l]
                                .compute_at(interpolated[parent], x)
                                .gpu_threads(x, y, c);
                        }
                    }

                    // The cpu wrapper is our new output Func
                    output_ = cpu_wrapper;
                }
            } else {
                for (int l = 1; l < levels-1; ++l) {
                    downsampled[l]
                        .compute_root()
                        .parallel(y, 8)
                        .vectorize(x, 4);
                    interpolated[l]
                        .compute_root()
                        .parallel(y, 8)
                        .unroll(x, 2)
                        .unroll(y, 2)
                        .vectorize(x, 8);
                }
                normalize
                    .reorder(c, x, y)
                    .bound(c, 0, 3)
                    .unroll(c)
                    .tile(x, y, xi, yi, 2, 2)
                    .unroll(xi)
                    .unroll(yi)
                    .parallel(y, 8)
                    .vectorize(x, 8)
                    .bound(x, 0, input_.width())
                    .bound(y, 0, input_.height());
                output_ = normalize;
            }
        }

        // Estimates (for autoscheduler; ignored otherwise)
        {
            input_.dim(0).set_bounds_estimate(0, 1536)
                  .dim(1).set_bounds_estimate(0, 2560)
                  .dim(2).set_bounds_estimate(0, 4);
            output_.dim(0).set_bounds_estimate(0, 1536)
                  .dim(1).set_bounds_estimate(0, 2560)
                  .dim(2).set_bounds_estimate(0, 3);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Interpolate, interpolate)
