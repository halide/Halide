#include "Halide.h"

using namespace Halide;

namespace halide_pytorch_ops {

Var x("x"), y("y"), c("c"), n("n");

template <typename Input>
Func add_(const Input &input_a, const Input &input_b) {
    Func output("f_output");
    output(x, y, c, n) = input_a(x, y, c, n) + input_b(x, y, c, n);
    return output;
}

class AddGenerator : public Generator<AddGenerator> {
public:
    Input<Buffer<>> input_a{"input_a", 4};
    Input<Buffer<>> input_b{"input_b", 4};
    Output<Buffer<>> output{"output", 4};

    void generate() {
        output(x, y, c, n) = add_(input_a, input_b)(x, y, c, n);

        Var tx("tx"), xy("xy"), cn("cn"), allvars("allvars");

        if (get_target().has_gpu_feature()) {
            output
                .fuse(x, y, xy)
                .fuse(c, n, cn)
                .fuse(xy, cn, allvars)
                .gpu_tile(allvars, tx, 128)
                ;
        } else {
            output
                .compute_root()
                .fuse(c, n, cn)
                .fuse(y, cn, allvars)
                .parallel(allvars, 8)
                .vectorize(x, 8)
                ;
        }
    }
};


class AddGradGenerator : public Generator<AddGradGenerator> {
public:
    Input<Buffer<>> input_a{"input_a", 4};
    Input<Buffer<>> input_b{"input_b", 4};
    Input<Buffer<>> d_output{"d_output", 4};

    Output<Buffer<>> d_input_a{"d_input_a", 4};
    Output<Buffer<>> d_input_b{"d_input_b", 4};

    void generate() {
        Func f_output = add_(input_a, input_b);

        // NOTE: the output_bounds argument is technically supposed to
        // be the shape of f_output; we'll use the bounds of input_a since it
        // is equivalent and easier to access.
        Derivative d = propagate_adjoints(f_output, d_output,
            {{0, input_a.dim(0).extent()}, 
             {0, input_a.dim(1).extent()}, 
             {0, input_a.dim(2).extent()}, 
             {0, input_a.dim(3).extent()}});

        d_input_a(x, y, c, n) = d(input_a)(x, y, c, n);
        d_input_b(x, y, c, n) = d(input_b)(x, y, c, n);

        Var tx("tx"), xy("xy"), cn("cn"), allvars("allvars");

        if (get_target().has_gpu_feature()) {
            d_input_a
                .fuse(x, y, xy)
                .fuse(c, n, cn)
                .fuse(xy, cn, allvars)
                .gpu_tile(allvars, tx, 128)
                ;
            d_input_b
                .fuse(x, y, xy)
                .fuse(c, n, cn)
                .fuse(xy, cn, allvars)
                .gpu_tile(allvars, tx, 128)
                ;
        } else {
            d_input_a
                .compute_root()
                .fuse(c, n, cn)
                .fuse(y, cn, allvars)
                .parallel(allvars, 8)
                .vectorize(x, 8)
                ;
            d_input_b
                .compute_root()
                .fuse(c, n, cn)
                .fuse(y, cn, allvars)
                .parallel(allvars, 8)
                .vectorize(x, 8)
                ;
        }
    }
};

}  // namespace halide_pytorch_ops


HALIDE_REGISTER_GENERATOR(halide_pytorch_ops::AddGenerator, add)
HALIDE_REGISTER_GENERATOR(halide_pytorch_ops::AddGradGenerator, add_grad)
