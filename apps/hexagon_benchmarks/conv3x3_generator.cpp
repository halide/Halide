#include "Halide.h"

using namespace Halide;

class Conv3x3 : public Generator<Conv3x3> {
public:
    GeneratorParam<Type> accumulator_type{"accumulator_type", Int(16)};
    // Takes an 8 bit image; one channel.
    Input<Buffer<uint8_t, 2>> input{"input"};
    Input<Buffer<int8_t, 2>> mask{"mask"};
    // Outputs an 8 bit image; one channel.
    Output<Buffer<uint8_t, 2>> output{"output"};

    GeneratorParam<bool> use_parallel_sched{"use_parallel_sched", true};
    GeneratorParam<bool> use_prefetch_sched{"use_prefetch_sched", true};

    void generate() {
        bounded_input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);

        Expr sum = cast(accumulator_type, 0);
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                sum += cast<int16_t>(bounded_input(x + j, y + i)) * cast<int16_t>(mask(j + 1, i + 1));
            }
        }
        output(x, y) = cast<uint8_t>(clamp(sum >> 4, 0, 255));
    }

    void schedule() {
        Var xi{"xi"}, yi{"yi"};

        input.dim(0).set_min(0);
        input.dim(1).set_min(0);

        output.dim(0).set_min(0);
        output.dim(1).set_min(0);

        if (get_target().has_feature(Target::HVX)) {
            const int vector_size = 128;
            Expr input_stride = input.dim(1).stride();
            input.dim(1).set_stride((input_stride / vector_size) * vector_size);

            Expr output_stride = output.dim(1).stride();
            output.dim(1).set_stride((output_stride / vector_size) * vector_size);
            bounded_input
                .compute_at(Func(output), y)
                .align_storage(x, 128)
                .vectorize(x, vector_size, TailStrategy::RoundUp);
            output
                .hexagon()
                .tile(x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp)
                .vectorize(xi)
                .unroll(yi);
            if (use_prefetch_sched) {
                output.prefetch(input, y, y, 2);
            }
            if (use_parallel_sched) {
                Var yo;
                output.split(y, yo, y, 128).parallel(yo);
            }
        } else {
            const int vector_size = natural_vector_size<uint8_t>();
            output
                .vectorize(x, vector_size)
                .parallel(y, 16);
        }
    }

private:
    Var x{"x"}, y{"y"};
    Func bounded_input{"input_bounded"};
};

HALIDE_REGISTER_GENERATOR(Conv3x3, conv3x3)
