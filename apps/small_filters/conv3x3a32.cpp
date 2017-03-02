#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

class Conv3x3a16 : public Generator<Conv3x3a16> {
public:
    // Takes an 8 bit image; one channel.
    Input<Buffer<uint8_t>> input{"input", 2};
    Input<Buffer<int8_t>> mask{"mask", 2};

    // Outputs an 8 bit image; one channel.
    Output<Buffer<uint8_t>> output{"output", 2};
    Var x{"x"}, y{"y"};
    void generate() {
        Func bounded_input{"input_bounded"};

        bounded_input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);

        Expr sum = cast<int32_t>(0);
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                sum += cast<int16_t>(bounded_input(x+j, y+i)) * cast<int16_t>(mask(j+1, i+1));
            }
        }
        output(x, y) = cast<uint8_t>(clamp(sum >> 4, 0, 255));
        bounded_input.compute_root();
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
            Func(output).hexagon().tile(x, y, xi, yi, vector_size, 2).vectorize(xi).unroll(yi);
        } else {
            const int vector_size = natural_vector_size<uint8_t>();
            Func(output).compute_root().vectorize(x, vector_size).parallel(y, 16);
        }
    }
};

HALIDE_REGISTER_GENERATOR(Conv3x3a16, "conv3x3a16");
