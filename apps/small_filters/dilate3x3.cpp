#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

class Dilate3x3 : public Generator<Dilate3x3> {
public:
    // Takes an 8 bit image; one channel.
    Input<Buffer<uint8_t>> input{"input", 2};
    // Outputs an 8 bit image; one channel.
    Output<Buffer<uint8_t>> output{"output", 2};
    Var x{"x"}, y{"y"};
    Func max_y{"max_y"};

    void generate() {
        Func bounded_input{"bounded_input"};

        bounded_input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);
        max_y(x, y) = max(bounded_input(x, y-1), max(bounded_input(x, y),
                                                     bounded_input(x, y+1)));

        output(x, y) = max(max_y(x-1, y), max(max_y(x, y), max_y(x+1, y)));
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
            Func(output).hexagon().tile(x, y, xi, yi, vector_size, 4).vectorize(xi).unroll(yi);
        } else {
            const int vector_size = natural_vector_size<uint8_t>();
            Func(output).compute_root().vectorize(x, vector_size).parallel(y, 16);
        }

    }
};

HALIDE_REGISTER_GENERATOR(Dilate3x3, "dilate3x3");



