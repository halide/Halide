#include "Halide.h"

using namespace Halide;

class Gaussian3x3 : public Generator<Gaussian3x3> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    GeneratorParam<bool> use_prefetch_sched{"use_prefetch_sched", true};

    void generate() {
        input_16(x, y) = cast<int16_t>(input(x, y));

        rows(x, y) = input_16(x, y-1) + 2 * input_16(x, y) + input_16(x, y+1);
        cols(x,y) =  rows(x-1, y) + 2 * rows(x, y) + rows(x+1, y);

        output(x, y)  = cast<uint8_t> ((cols(x, y) + 8) >> 4);
    }

    void schedule() {
        Var xi{"xi"}, yi{"yi"}, yo{"yo"};

        // input.dim(0).set_min(0);
        // input.dim(1).set_min(0);

        output.dim(0).set_min(0);
        output.dim(1).set_min(0);

        if (get_target().features_any_of({Target::HVX})) {
            const int vector_size = 128;

            output
                .hexagon()
#ifndef OLD_PREFETCH
                .prefetch(input, y, y, 2, PrefetchBoundStrategy::NonFaulting)
#else
                .prefetch(input, y, 2, PrefetchBoundStrategy::NonFaulting)
#endif
                .tile(x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp)
                .vectorize(xi)
                .unroll(yi);
        } else {
            const int vector_size = natural_vector_size<uint8_t>();
            output
                .tile(x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp)
                .vectorize(xi)
                .unroll(yi);
        }
    }
private:
    Var x{"x"}, y{"y"};
    Func rows{"rows"}, cols{"cols"};
    Func input_16{"input_16"};
    Func bounded_input{"bounded_input"}; // TODO: should this be scheduled??
};

HALIDE_REGISTER_GENERATOR(Gaussian3x3, gaussian3x3);
