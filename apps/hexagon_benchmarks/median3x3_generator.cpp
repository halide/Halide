#include "Halide.h"

using namespace Halide;

class Median3x3 : public Generator<Median3x3> {
private:
    static Expr mid(Expr a, Expr b, Expr c) {
        return max(min(max(a, b), c), min(a, b));
    }

public:
    // Takes an 8 bit image; one channel.
    Input<Buffer<uint8_t, 2>> input{"input"};
    // Outputs an 8 bit image; one channel.
    Output<Buffer<uint8_t, 2>> output{"output"};

    GeneratorParam<bool> use_parallel_sched{"use_parallel_sched", true};
    GeneratorParam<bool> use_prefetch_sched{"use_prefetch_sched", true};

    void generate() {
        bounded_input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);
        max_y(x, y) = max(bounded_input(x, y - 1), bounded_input(x, y), bounded_input(x, y + 1));
        min_y(x, y) = min(bounded_input(x, y - 1), bounded_input(x, y), bounded_input(x, y + 1));
        mid_y(x, y) = mid(bounded_input(x, y - 1), bounded_input(x, y), bounded_input(x, y + 1));

        minmax_x(x, y) = min(max_y(x - 1, y), max_y(x, y), max_y(x + 1, y));
        maxmin_x(x, y) = max(min_y(x - 1, y), min_y(x, y), min_y(x + 1, y));
        midmid_x(x, y) = mid(mid_y(x - 1, y), mid_y(x, y), mid_y(x + 1, y));

        output(x, y) = mid(minmax_x(x, y), maxmin_x(x, y), midmid_x(x, y));
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
                .tile(x, y, xi, yi, vector_size, 4)
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
    Func max_y{"max_y"}, min_y{"min_y"}, mid_y{"mid_y"};
    Func minmax_x{"minmax_x"}, maxmin_x{"maxmin_x"}, midmid_x{"midmid_x"};
    Func bounded_input{"bounded_input"};
};

HALIDE_REGISTER_GENERATOR(Median3x3, median3x3)
