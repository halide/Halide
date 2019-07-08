#include "Halide.h"

using namespace Halide;

namespace halide_pytorch_ops {

class AddGenerator : public Generator<AddGenerator> {
public:
    Input<Buffer<float>> input_a{"input_a", 4};
    Input<Buffer<float>> input_b{"input_b", 4};
    Output<Buffer<float>> output{"output", 4};


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

}  // namespace halide_pytorch_ops


HALIDE_REGISTER_GENERATOR(halide_pytorch_ops::AddGenerator, add)
