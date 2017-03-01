#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

class Gaussian5x5 : public Generator<Gaussian5x5> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};

    Output<Buffer<uint8_t>> output{"output", 2};

    Func rows{"rows"}, cols{"cols"};
    Var x{"x"}, y{"y"};

    void generate() {
        Func bounded_input{"bounded_input"};
        bounded_input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);

        Func input_16("input_16");
        input_16(x, y) = cast<int16_t>(bounded_input(x, y));

        rows(x, y) = input_16(x, y-2) + 4*input_16(x, y-1) + 6*input_16(x,y)+ 4*input_16(x,y+1) + input_16(x,y+2);
        cols(x,y) =  rows(x-2, y) + 4*rows(x-1, y) + 6*rows(x, y) + 4*rows(x+1, y) + rows(x+2, y);

        output(x, y)  = cast<uint8_t> (cols(x, y) >> 8);
    }
    void schedule() {
        Var xi{"xi"}, yi{"yi"};
        auto set_min_0 = [this](int dim) {
            input.dim(dim).set_min(0);
            auto output_buff = Func(output).output_buffer();
            output_buff.dim(dim).set_min(0);
        };
        set_min_0(0);
        set_min_0(1);

        if (get_target().features_any_of({Target::HVX_64, Target::HVX_128})) {
            const int vector_size = get_target().has_feature(Target::HVX_128) ? 128 : 64;
            auto set_aligned_stride = [this](int dim, int alignment) {
                Expr input_stride = input.dim(dim).stride();
                input.dim(dim).set_stride((input_stride/alignment) * alignment);

                auto output_buff = Func(output).output_buffer();
                Expr output_stride = output_buff.dim(dim).stride();
                output_buff.dim(dim).set_stride((output_stride/alignment) * alignment);
            };
            set_aligned_stride(1, vector_size);
            Func(output).hexagon().tile(x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp).vectorize(xi).unroll(yi);
            rows.compute_at(Func(output), y)
                .tile(x, y, x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp)
                .vectorize(xi)
                .unroll(yi);
        } else {
            const int vector_size = natural_vector_size<uint8_t>();
            Func(output).compute_root().vectorize(x, vector_size).parallel(y, 16);
        }
    }
};

HALIDE_REGISTER_GENERATOR(Gaussian5x5, "gaussian5x5");
