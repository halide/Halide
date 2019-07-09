#include "Halide.h"

using namespace Halide;

namespace halide_pytorch_ops {

class AddGenerator : public Generator<AddGenerator> {
public:
    Input<Func> input_a{"input_a", 4};
    Input<Func> input_b{"input_b", 4};
    Output<Func> output{"output", 4};

    void generate() {
        Var x("x"), y("y"), c("c"), n("n");
        output(x, y, c, n) = input_a(x, y, c, n) + input_b(x, y, c, n);

        Var tx("tx"), xy("xy"), cn("cn"), allvars("allvars");

        if(get_target().has_gpu_feature()) {
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
    Input<Func> d_output{"d_output", 4};

    Output<Func> d_input_a{"d_input_a", 4};
    Output<Func> d_input_b{"d_input_b", 4};

    void generate() {
        Var x("x"), y("y"), c("c"), n("n");

        d_input_a(x, y, c, n) = d_output(x, y, c, n);
        d_input_b(x, y, c, n) = d_output(x, y, c, n);

        Var tx("tx"), xy("xy"), cn("cn"), allvars("allvars");

        if(get_target().has_gpu_feature()) {
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
