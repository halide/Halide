#include "Halide.h"
#include "../autoscheduler/SimpleAutoSchedule.h"

namespace {

class MatMul : public Halide::Generator<MatMul> {
public:
    GeneratorParam<int>     matrix_size{"matrix_size", 1536};

    Input<Buffer<float>>    input_a{"input_a", 2};
    Input<Buffer<float>>    input_b{"input_b", 2};

    Output<Buffer<float>>   output{"output", 2};

    void generate() {
        Var x("x"), y("y");

        // Algorithm
        RDom k(0, matrix_size);

        Func matrix_mul("matrix_mul");
        matrix_mul(x, y) += input_a(k, y) * input_b(x, k);

        output(x, y) = matrix_mul(x, y);

        // Schedule
        if (!auto_schedule) {
            std::string use_simple_autoscheduler =
                Halide::Internal::get_env_variable("HL_USE_SIMPLE_AUTOSCHEDULER");
            if (use_simple_autoscheduler == "1") {
                Halide::SimpleAutoscheduleOptions options;
                options.gpu = get_target().has_gpu_feature();
                options.gpu_tile_channel = 1;
                Func output_func = output;
                Halide::simple_autoschedule(output_func,
                                    {{"input_a.min.0", 0},
                                     {"input_a.extent.0", matrix_size},
                                     {"input_a.min.1", 0},
                                     {"input_a.extent.1", matrix_size},
                                     {"input_b.min.0", 0},
                                     {"input_b.extent.0", matrix_size},
                                     {"input_b.min.1", 0},
                                     {"input_b.extent.1", matrix_size}},
                                    {{0, matrix_size},
                                     {0, matrix_size}},
                                    options);
            } else {
                Var xi("xi"), yi("yi"), yii("yii"), xii("xii"), xy("xy");
                output.tile(x, y, xi, yi, 24, 32)
                    .fuse(x, y, xy)
                    .parallel(xy)
                    .split(yi, yi, yii, 4)
                    .vectorize(xi, 8)
                    .unroll(xi)
                    .unroll(yii);

                matrix_mul.compute_at(output, yi)
                    .vectorize(x, 8).unroll(y);

                matrix_mul.update(0)
                    .reorder(x, y, k)
                    .vectorize(x, 8)
                    .unroll(x)
                    .unroll(y)
                    .unroll(k, 2);
            }
        }

        // Always specify bounds for outputs, whether autoscheduled or not
        output
            .bound(x, 0, matrix_size)
            .bound(y, 0, matrix_size);

        // Estimates
        {
            input_a.dim(0).set_bounds_estimate(0, matrix_size)
                   .dim(1).set_bounds_estimate(0, matrix_size);
            input_b.dim(0).set_bounds_estimate(0, matrix_size)
                   .dim(1).set_bounds_estimate(0, matrix_size);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, mat_mul)
