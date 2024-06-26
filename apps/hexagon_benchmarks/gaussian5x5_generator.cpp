#include "Halide.h"

using namespace Halide;

class Gaussian5x5 : public Generator<Gaussian5x5> {
public:
    Input<Buffer<uint8_t, 2>> input{"input"};
    Output<Buffer<uint8_t, 2>> output{"output"};

    GeneratorParam<bool> use_parallel_sched{"use_parallel_sched", true};
    GeneratorParam<bool> use_prefetch_sched{"use_prefetch_sched", true};

    void generate() {
        bounded_input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);

        Func input_16("input_16");
        input_16(x, y) = cast<int16_t>(bounded_input(x, y));

        rows(x, y) = input_16(x, y - 2) + 4 * input_16(x, y - 1) + 6 * input_16(x, y) + 4 * input_16(x, y + 1) + input_16(x, y + 2);
        cols(x, y) = rows(x - 2, y) + 4 * rows(x - 1, y) + 6 * rows(x, y) + 4 * rows(x + 1, y) + rows(x + 2, y);

        output(x, y) = cast<uint8_t>(cols(x, y) >> 8);
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
            bounded_input
                .compute_at(Func(output), y)
                .align_storage(x, 128)
                .vectorize(x, vector_size, TailStrategy::RoundUp);
            output.dim(1).set_stride((output_stride / vector_size) * vector_size);
            output
                .hexagon()
                .tile(x, y, xi, yi, vector_size * 2, 4, TailStrategy::RoundUp)
                .vectorize(xi)
                .unroll(yi);
            rows.compute_at(Func(output), y)
                .tile(x, y, x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp)
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
    Func rows{"rows"}, cols{"cols"}, bounded_input{"bounded_input"};
    Var x{"x"}, y{"y"};
};

HALIDE_REGISTER_GENERATOR(Gaussian5x5, gaussian5x5)
