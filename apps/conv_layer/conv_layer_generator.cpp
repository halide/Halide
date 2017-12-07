#include "Halide.h"

namespace {

using namespace Halide;

class ConvolutionLayer : public Halide::Generator<ConvolutionLayer> {
public:
    Input<Buffer<float>>  input{"input", 4};
    Input<Buffer<float>>  filter{"filter", 4};
    Input<Buffer<float>>  bias{"bias", 1};

    Output<Buffer<float>> f_ReLU{"ReLU", 4};

    void generate() {
        /* THE ALGORITHM */

        Var x("x"), y("y"), z("z"), n("n");

        Func f_conv("conv");
        RDom r(filter.dim(0).min(), filter.dim(0).extent(),
               filter.dim(1).min(), filter.dim(1).extent(),
               filter.dim(2).min(), filter.dim(2).extent());

        f_conv(x, y, z, n) = bias(z);

        f_conv(x, y, z, n) += filter(r.x, r.y, r.z, z) * input(x + r.x, y + r.y, r.z, n);

        f_ReLU(x, y, z, n) = max(0, f_conv(x, y, z, n));

        /* THE SCHEDULE */

        if (auto_schedule) {
            // Provide estimates on the input image
            input.dim(0).set_bounds_estimate(0, 131);
            input.dim(1).set_bounds_estimate(0, 131);
            input.dim(2).set_bounds_estimate(0, 64);
            input.dim(3).set_bounds_estimate(0, 4);

            filter.dim(0).set_bounds_estimate(0, 3);
            filter.dim(1).set_bounds_estimate(0, 3);
            filter.dim(2).set_bounds_estimate(0, 64);
            filter.dim(3).set_bounds_estimate(0, 64);

            bias.dim(0).set_bounds_estimate(0, 64);

            // Provide estimates on the pipeline f_ReLU
            f_ReLU.estimate(x, 0, 128)
                .estimate(y, 0, 128)
                .estimate(z, 0, 64)
                .estimate(n, 0, 4);

        } /*else if (get_target().has_gpu_feature()) {
            // TODO: Turn off the manual GPU schedule for now.
            // For some reasons, it sometimes triggers the (err == CL_SUCCESS)
            // assertion on mingw.
            Var ni, no, xi, xo, yi, yo, zi, zo;
            f_ReLU.compute_root()
                .split(x, xo, xi, 4)
                .split(y, yo, yi, 4)
                .split(z, zo, zi, 4)
                .reorder(xi, yi, zi, n, xo, yo, zo)
                .gpu_threads(xi, yi, zi)
                .gpu_blocks(xo, yo, zo);

            f_conv.compute_at(f_ReLU, n)
                .gpu_threads(x, y, z)
                .update()
                .unroll(r.x, 3)
                .unroll(r.y, 3)
                .gpu_threads(x, y, z);
        }*/ else {
            // Blocking spatially with vectorization
            Var z_t("z_t"), y_t("y_t"), par("par");
            int vec_len = 8;
            int o_block_size = 32;
            int y_block = 32;
            f_conv.compute_root();
            f_conv.fuse(z, n, par).parallel(par);
            f_conv.update()
                .reorder(x, y, r.z)
                .split(y, y, y_t, y_block)
                .split(z, z, z_t, o_block_size)
                .reorder(y_t, z_t, y, r.z, z)
                .vectorize(x, vec_len)
                .unroll(r.x, 3)
                .unroll(r.y, 3)
                .fuse(z, n, par)
                .parallel(par);
            f_ReLU.reorder(n, z).parallel(z).vectorize(x, 8);
        }
   }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ConvolutionLayer, conv_layer)

