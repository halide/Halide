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
            input.dim(0).set_bounds_estimate(0, CI);
            input.dim(1).set_bounds_estimate(0, W + 2);
            input.dim(2).set_bounds_estimate(0, H + 2);
            input.dim(3).set_bounds_estimate(0, N);

            filter.dim(0).set_bounds_estimate(0, CO);
            filter.dim(1).set_bounds_estimate(0, 3);
            filter.dim(2).set_bounds_estimate(0, 3);
            filter.dim(3).set_bounds_estimate(0, CI);

            bias.dim(0).set_bounds_estimate(0, CO);

            // Provide estimates on the pipeline f_ReLU
            f_ReLU.estimate(x, 0, W)
                .estimate(y, 0, H)
                .estimate(c, 0, CO)
                .estimate(n, 0, N);

        } else {
            // Best schedule (optimized for N = 4, CI = 64, CO = 64, W = 128, H = 128)
            /*
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
            */

            /* For that size, MKL uses the schedule below, but LLVM won't register-allocate it properly :(
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

            // Best schedule for (N = 5, CI = 120, CO = 24, W = 120, H
            // = 80).
            Var co, ci, xo, xi, yo, yi, t;
            f_ReLU.split(c, co, ci, 24)
                .split(x, xo, xi, 4)
                .reorder(ci, xi, xo, y, n, co)
                .vectorize(ci, 8).unroll(ci).unroll(xi)
                .parallel(y).parallel(n).parallel(co);
            f_conv.compute_at(f_ReLU, xo)
                .vectorize(c, 8).unroll(c).unroll(x).unroll(y)
                .update().reorder(c, x, y, r.x, r.y, r.z, n)
                .vectorize(c, 8).unroll(c).unroll(x).unroll(y).unroll(r.x, 2);
            filter.in().compute_at(f_conv, r.x).vectorize(_0, 8).unroll(_0).unroll(_3);
            input.in().compute_at(f_conv, x).unroll(_0);

            // Sane schedule that should work for any size
            // f_ReLU.parallel(n).parallel(y).vectorize(c, 8);

        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ConvolutionLayer, conv_layer)
