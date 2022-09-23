#include "Halide.h"

using namespace Halide;

using Halide::ConciseCasts::u8_sat;

class Gaussian7x7 : public Generator<Gaussian7x7> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    void generate() {
        bounded_input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);

        input_32(x, y) = cast<int32_t>(bounded_input(x, y));

        if (Halide::Internal::get_env_variable("HL_ENABLE_RAKE") == "1") {
            Expr a = bounded_input(x, y - 3);
            Expr b = bounded_input(x, y - 2);
            Expr c = bounded_input(x, y - 1);
            Expr d = bounded_input(x, y);
            Expr e = bounded_input(x, y + 1);
            Expr f = bounded_input(x, y + 2);
            Expr g = bounded_input(x, y + 3);
            rows(x, y) = (widening_add(widening_mul(b, cast<uint8_t>(6)), widening_mul(c, cast<uint8_t>(15)))
                          + widening_add(widening_mul(d, cast<uint8_t>(20)), widening_mul(e, cast<uint8_t>(15))))
                          + widening_add(widening_mul(f, cast<uint8_t>(6)), widening_add(a, g));
        } else {
            rows(x, y) = input_32(x, y-3) + input_32(x, y-2) * cast<uint8_t>(6) + input_32(x, y-1) * cast<uint8_t>(15) + input_32(x, y) * cast<uint8_t>(20) + 
                            input_32(x, y+1) * cast<uint8_t>(15) + input_32(x, y+2) * cast<uint8_t>(6) + input_32(x, y+3);
        }

        cols(x,y) =  rows(x-3, y) + rows(x-2, y) * 6 + rows(x-1, y) * 15 + rows(x, y) * 20 + rows(x+1, y) * 15 + 
                        rows(x+2, y) * 6 + rows(x+3, y);

        output(x, y)  = u8_sat(cols(x, y) >> 12);
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
        rows.compute_at(Func(output), y)
            .tile(x, y, x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp)
            .vectorize(xi)
            .unroll(yi)
            .align_storage(x, 128);
    }
    
private:
    Var x{"x"}, y{"y"};
    Func rows{"rows"}, cols{"cols"};
    Func input_32{"input_32"}, bounded_input{"bounded_input"};
};

HALIDE_REGISTER_GENERATOR(Gaussian7x7, gaussian7x7);
