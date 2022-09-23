#include "Halide.h"
#include "utils.h"
using namespace Halide;

class Median3x3 : public Generator<Median3x3> {
private:
    static Expr mid(Expr a, Expr b, Expr c) {
        return max(min(max(a, b), c), min(a, b));
    }

public:
    // Takes an 8 bit image; one channel.
    Input<Buffer<uint8_t>> input{"input", 2};
    // Outputs an 8 bit image; one channel.
    Output<Buffer<uint8_t>> output{"output", 2};

    GeneratorParam<bool> use_parallel_sched{"use_parallel_sched", true};
    GeneratorParam<bool> use_prefetch_sched{"use_prefetch_sched", true};

    void generate() {
        Expr height = input.height();
        bounded_input(x, y) = repeat_edge_x(input)(x, y);
        max_y(x,y) = max(bounded_input(x ,clamp(y-1, 0, height-1)),
                         bounded_input(x, clamp(y, 0, height-1)),
                         bounded_input(x, clamp(y+1, 0, height-1)));
        min_y(x,y) = min(bounded_input(x, clamp(y-1, 0, height-1)),
                         bounded_input(x, clamp(y, 0, height-1)),
                         bounded_input(x, clamp(y+1, 0, height-1)));
        mid_y(x,y) = mid(bounded_input(x, clamp(y-1, 0, height-1)),
                         bounded_input(x, clamp(y, 0, height-1)),
                         bounded_input(x, clamp(y+1, 0, height-1)));

        minmax_x(x,y) = min(max_y(x-1, y), max_y(x, y), max_y(x+1, y));
        maxmin_x(x,y) = max(min_y(x-1, y), min_y(x, y), min_y(x+1, y));
        midmid_x(x,y) = mid(mid_y(x-1, y), mid_y(x, y), mid_y(x+1, y));

        output(x,y) = mid(minmax_x(x, y), maxmin_x(x, y), midmid_x(x, y));
    }

    void schedule() {
        Var xi{"xi"}, yi{"yi"};

        input.dim(0).set_min(0);
        input.dim(1).set_min(0);

        output.dim(0).set_min(0);
        output.dim(1).set_min(0);

        const int vector_size = natural_vector_size<uint8_t>();
        bounded_input
            .compute_at(Func(output), y)
            .align_storage(x, 128)
            .vectorize(x, vector_size, TailStrategy::RoundUp);
        output
            .tile(x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp)
            .vectorize(xi)
            .unroll(yi);
    }
private:
    Var x{"x"}, y{"y"}, yo{"yo"};
    Func max_y{"max_y"}, min_y{"min_y"}, mid_y{"mid_y"};
    Func minmax_x{"minmax_x"}, maxmin_x{"maxmin_x"}, midmid_x{"midmid_x"};
    Func bounded_input{"bounded_input"};
};

HALIDE_REGISTER_GENERATOR(Median3x3, median3x3)