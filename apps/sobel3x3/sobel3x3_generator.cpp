#include "Halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

class Sobel : public Generator<Sobel> {
public:
    Input<Buffer<uint8_t>> input{ "input", 2 };
    Output<Buffer<uint8_t>> output{ "output", 2 };

    void generate() {
        bounded_input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);

        Func input_16{ "input_16" };
        input_16(x, y) = cast<uint16_t>(bounded_input(x, y));

        if (Halide::Internal::get_env_variable("HL_ENABLE_RAKE") == "1") {
            Expr a = bounded_input(x - 1, y);
            Expr b = bounded_input(x, y);
            Expr c = bounded_input(x + 1, y);
            sobel_x_avg(x, y) = widening_add(a, c) + widening_mul(b, cast<uint8_t>(2));
            sobel_x(x, y) = absd(sobel_x_avg(x, y - 1), sobel_x_avg(x, y + 1));

            Expr d = bounded_input(x, y - 1);
            Expr e = bounded_input(x, y);
            Expr f = bounded_input(x, y + 1);
            sobel_y_avg(x, y) = widening_add(d, f) + widening_mul(e, cast<uint8_t>(2));
            sobel_y(x, y) = absd(sobel_y_avg(x - 1, y), sobel_y_avg(x + 1, y));

            // This sobel implementation is non-standard in that it doesn't take the square root
            // of the gradient.
            output(x, y) = u8_sat(sobel_x(x, y) + sobel_y(x, y));
        } else {
            sobel_x_avg(x, y) = input_16(x - 1, y) + 2 * input_16(x, y) + input_16(x + 1, y);
            sobel_x(x, y) = absd(sobel_x_avg(x, y - 1), sobel_x_avg(x, y + 1));

            sobel_y_avg(x, y) = input_16(x, y - 1) + 2 * input_16(x, y) + input_16(x, y + 1);
            sobel_y(x, y) = absd(sobel_y_avg(x - 1, y), sobel_y_avg(x + 1, y));

            // This sobel implementation is non-standard in that it doesn't take the square root
            // of the gradient.
            output(x, y) = cast<uint8_t>(clamp(sobel_x(x, y) + sobel_y(x, y), 0, 255));
        }
    }

    void schedule() {
        Var xi{"xi"}, yi{"yi"};

        input.dim(0).set_min(0);
        input.dim(1).set_min(0);

        output.dim(0).set_min(0);
        output.dim(1).set_min(0);

        if (get_target().has_feature(Target::HVX)) {
            // const int vector_size = 128;
            const int vector_size = 256;
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
        } else {
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
    }

private:
    Var x{ "x" }, y{ "y" };
    Func sobel_x_avg{ "sobel_x_avg" }, sobel_y_avg{ "sobel_y_avg" };
    Func sobel_x{ "sobel_x" }, sobel_y{ "sobel_y" };
    Func bounded_input{ "bounded_input" };
};

HALIDE_REGISTER_GENERATOR(Sobel, sobel3x3)