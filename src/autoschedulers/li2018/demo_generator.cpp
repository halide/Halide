#include "Halide.h"

namespace {

using namespace Halide;

class ConvRelu : public Halide::Generator<ConvRelu> {
public:
    Input<Buffer<float>> input{"input", 4};
    Input<Buffer<float>> filter{"filter", 4};
    Input<Buffer<float>> bias{"bias", 1};
    Output<Buffer<float>> relu{"relu", 4};

    void generate() {
        const int N = 5, CI = 120, CO = 24, W = 100, H = 80;

        Var x("x"), y("y"), c("c"), n("n");

        Func conv("conv");
        RDom r(0, CI, 0, 3, 0, 3);
        conv(c, x, y, n) = bias(c);
        conv(c, x, y, n) += filter(c, r.y, r.z, r.x) * input(r.x, x + r.y, y + r.z, n);
        relu(c, x, y, n) = max(0, conv(c, x, y, n));

        relu.bound(c, 0, CO)
            .bound(x, 0, W)
            .bound(y, 0, H)
            .bound(n, 0, N);

        relu.dim(0).set_bounds(0, CO).set_stride(1);
        relu.dim(1).set_bounds(0, W).set_stride(CO);
        relu.dim(2).set_bounds(0, H).set_stride(CO * W);
        relu.dim(3).set_bounds(0, N).set_stride(CO * H * W);

        input.dim(0).set_bounds(0, CI).set_stride(1);
        input.dim(1).set_bounds(0, W + 2).set_stride(CI);
        input.dim(2).set_bounds(0, H + 2).set_stride(CI * (W + 2));
        input.dim(3).set_bounds(0, N).set_stride(CI * (W + 2) * (H + 2));

        filter.dim(0).set_bounds(0, CO).set_stride(1);
        filter.dim(1).set_bounds(0, 3).set_stride(CO);
        filter.dim(2).set_bounds(0, 3).set_stride(CO * 3);
        filter.dim(3).set_bounds(0, CI).set_stride(CO * 3 * 3);

        bias.dim(0).set_bounds(0, CO).set_stride(1);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ConvRelu, demo)
