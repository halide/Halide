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

        Var x("x"), y("y"), c("c"), n("n");

        Func f_conv("conv");
        RDom r(0, 64, 0, 3, 0, 3);

        f_conv(c, x, y, n) = bias(c);

        f_conv(c, x, y, n) += filter(c, r.y, r.z, r.x) * input(r.x, x + r.y, y + r.z, n);

        f_ReLU(c, x, y, n) = max(0, f_conv(c, x, y, n));

        /*
        Func f_conv("conv");
        RDom r(0, 64, 0, 3, 0, 3);

        f_conv(x, y, c, n) = bias(c);

        f_conv(x, y, c, n) += filter(r.y, r.z, r.x, c) * input(x + r.y, y + r.z, r.x, n);

        f_ReLU(x, y, c, n) = max(0, f_conv(x, y, c, n));
        */

        /* THE SCHEDULE */

        f_ReLU.bound(x, 0, 128)
            .bound(y, 0, 128)
            .bound(c, 0, 64)
            .bound(n, 0, 4);

        f_ReLU.dim(0).set_bounds(0, 64).set_stride(1)
            .dim(1).set_bounds(0, 128).set_stride(64)
            .dim(2).set_bounds(0, 128).set_stride(64 * 128)
            .dim(3).set_bounds(0, 4).set_stride(64 * 128 * 128);

        input.dim(0).set_bounds(0, 64).set_stride(1)
            .dim(1).set_bounds(0, 130).set_stride(64)
            .dim(2).set_bounds(0, 130).set_stride(64 * 130)
            .dim(3).set_bounds(0, 4).set_stride(64 * 130 * 130);

        filter.dim(0).set_bounds(0, 64).set_stride(1)
            .dim(1).set_bounds(0, 3).set_stride(64)
            .dim(2).set_bounds(0, 3).set_stride(64 * 3)
            .dim(3).set_bounds(0, 64).set_stride(64 * 3 * 3);

        bias.dim(0).set_bounds(0, 64).set_stride(1);

        if (auto_schedule) {
            // Provide estimates on the input image
            input.dim(0).set_bounds_estimate(0, 64);
            input.dim(1).set_bounds_estimate(0, 128 + 2);
            input.dim(2).set_bounds_estimate(0, 128 + 2);
            input.dim(3).set_bounds_estimate(0, 4);

            filter.dim(0).set_bounds_estimate(0, 64);
            filter.dim(1).set_bounds_estimate(0, 3);
            filter.dim(2).set_bounds_estimate(0, 3);
            filter.dim(3).set_bounds_estimate(0, 64);

            bias.dim(0).set_bounds_estimate(0, 64);

            // Provide estimates on the pipeline f_ReLU
            f_ReLU.estimate(x, 0, 128)
                .estimate(y, 0, 128)
                .estimate(c, 0, 64)
                .estimate(n, 0, 4);



        } /*else if (get_target().has_gpu_feature()) {
            // TODO: Turn off the manual GPU schedule for now.
            // For some reasons, it sometimes triggers the (err == CL_SUCCESS)
            // assertion on mingw.
            Var ni, no, xi, xo, yi, yo, ci, co;
            f_ReLU.compute_root()
                .split(x, xo, xi, 4)
                .split(y, yo, yi, 4)
                .split(c, co, ci, 4)
                .reorder(xi, yi, ci, n, xo, yo, co)
                .gpu_threads(xi, yi, ci)
                .gpu_blocks(xo, yo, co);

            f_conv.compute_at(f_ReLU, n)
                .gpu_threads(x, y, c)
                .update()
                .unroll(r.x, 3)
                .unroll(r.y, 3)
                .gpu_threads(x, y, c);
        }*/ else {

            // Best schedule
            Var co, ci, xo, xi, yo, yi, t;
            f_ReLU.split(c, co, ci, 16)
                .split(x, xo, xi, 3)
                .split(y, yo, yi, 2)
                .reorder(ci, xi, yi, xo, yo, n, co)
                .vectorize(ci).unroll(yi).unroll(xi)
                .parallel(yo).parallel(n).parallel(co);
            f_conv.compute_at(f_ReLU, xo)
                .vectorize(c).unroll(x).unroll(y).update()
                .reorder(c, x, y, r.x, r.y, r.z, n)
                .vectorize(c).unroll(x).unroll(y).unroll(r.x, 2);
            filter.in().compute_at(f_conv, r.x).vectorize(_0);

            /* MKL uses the schedule below, but LLVM won't register-allocate it properly
            Var co, ci, cio, cii, xo, xi, yo, yi, t;
            f_ReLU.split(c, co, ci, 32).split(ci, cio, cii, 8)
                .split(x, xo, xi, 3)
                .reorder(cii, xi, cio, xo, y, co, n)
                .vectorize(cii).unroll(cio).unroll(xi)
                .fuse(n, co, t).parallel(t);
            RVar rxo, rxi, ryo, ryi;
            f_conv.compute_at(f_ReLU, xo)
                .vectorize(c, 8).unroll(c).unroll(x).unroll(y)
                .update()
                .split(r.x, rxo, rxi, 8)
                .split(c, co, ci, 8)
                .reorder(ci, x, co, rxi, r.y, rxo, r.z, y, n)
                .vectorize(ci).unroll(x).unroll(co).unroll(rxi).unroll(r.y);
            input.in().compute_at(f_conv, rxi).unroll(_1);
            filter.in().compute_at(f_conv, co).vectorize(_0).unroll(_3);
            */

        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ConvolutionLayer, conv_layer)
