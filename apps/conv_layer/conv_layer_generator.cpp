#include "Halide.h"

namespace {

using namespace Halide;

class ConvolutionLayer : public Halide::Generator<ConvolutionLayer> {
public:
    Input<Buffer<float>> input{"input", 4};
    Input<Buffer<float>> filter{"filter", 4};
    Input<Buffer<float>> bias{"bias", 1};

    Output<Buffer<float>> f_ReLU{"ReLU", 4};

    void generate() {
        // const int N = 4, CI = 64, CO = 64, W = 128, H = 128;
        const int N = 5, CI = 120, CO = 24, W = 100, H = 80;

        Var x("x"), y("y"), c("c"), n("n");

        Func f_conv("conv");
        RDom r(0, CI, 0, 3, 0, 3);

        f_conv(c, x, y, n) = bias(c);
        f_conv(c, x, y, n) += filter(c, r.y, r.z, r.x) * input(r.x, x + r.y, y + r.z, n);

        f_ReLU(c, x, y, n) = max(0, f_conv(c, x, y, n));

        // MKL JITs code for the specific size and strides, so we'll
        // do the same and ask Halide to compile for this specific
        // size:
        f_ReLU.bound(x, 0, W)
            .bound(y, 0, H)
            .bound(c, 0, CO)
            .bound(n, 0, N);

        f_ReLU.dim(0).set_bounds(0, CO).set_stride(1)
            .dim(1).set_bounds(0, W).set_stride(CO)
            .dim(2).set_bounds(0, H).set_stride(CO * W)
            .dim(3).set_bounds(0, N).set_stride(CO * H * W);

        input.dim(0).set_bounds(0, CI).set_stride(1)
            .dim(1).set_bounds(0, W + 2).set_stride(CI)
            .dim(2).set_bounds(0, H + 2).set_stride(CI * (W + 2))
            .dim(3).set_bounds(0, N).set_stride(CI * (W + 2) * (H + 2));

        filter.dim(0).set_bounds(0, CO).set_stride(1)
            .dim(1).set_bounds(0, 3).set_stride(CO)
            .dim(2).set_bounds(0, 3).set_stride(CO * 3)
            .dim(3).set_bounds(0, CI).set_stride(CO * 3 * 3);

        bias.dim(0).set_bounds(0, CO).set_stride(1);

        if (auto_schedule) {
            // Provide estimates on the input image
            input.set_estimates({{0, CI}, {0, W + 2}, {0, H + 2}, {0, N}});
            filter.set_estimates({{0, CO}, {0, 3}, {0, 3}, {0, CI}});
            bias.set_estimates({{0, CO}});
            f_ReLU.set_estimates({{0, CO}, {0, W}, {0, H}, {0, N}});

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
        }*/
        else {
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
