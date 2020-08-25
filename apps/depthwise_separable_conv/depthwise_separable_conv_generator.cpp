#include "Halide.h"

namespace {

using namespace Halide;
using namespace Halide::BoundaryConditions;

class DepthwiseSeparableConvolution : public Generator<DepthwiseSeparableConvolution> {
public:
    // [in_channels, width, height, batch_size]
    Input<Buffer<float>> input{"input", 4};

    // [channel_multiplier, in_channels, filter_width, filter_height]
    Input<Buffer<float>> depthwise_filter{"depthwise_filter", 4};

    // [out_channels, channel_multiplier * in_channels]
    Input<Buffer<float>> pointwise_filter{"pointwise_filter", 2};

    // [out_channels]
    Input<Buffer<float>> bias{"bias", 1};

    // [out_channels, width, height, batch_size]
    Output<Buffer<float>> output{"output", 4};

    void generate() {
        // The algorithm. It will be a generic depthwise convolution,
        // with no assumptions about input sizes or shapes. This makes
        // it especially challenging to schedule.

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), d("d"), b("b");

        // Pad x and y with 0
        Func input_bounded =
            BoundaryConditions::constant_exterior(input,
                                                  0.0f,
                                                  {{Expr(), Expr()},
                                                   {0, input.dim(1).extent()},
                                                   {0, input.dim(2).extent()}});

        Expr channel_multiplier = depthwise_filter.dim(0).extent();

        // Convolve the image depthwise -- for each input channel,
        // generate channel_multiplier number of intermediate channels using convolution
        Func depthwise_convolved("depthwise_convolved");
        Expr pad_width = depthwise_filter.dim(2).extent() / 2;
        Expr pad_height = depthwise_filter.dim(3).extent() / 2;
        RDom depthwise_filter_dom(0, depthwise_filter.dim(0).extent(),
                                  0, depthwise_filter.dim(2).extent(),
                                  0, depthwise_filter.dim(3).extent());
        depthwise_convolved(d, x, y, b) +=
            depthwise_filter(depthwise_filter_dom[0],
                             d,
                             depthwise_filter_dom[1],
                             depthwise_filter_dom[2]) *
            input_bounded(d / channel_multiplier,
                          x + depthwise_filter_dom[1] - pad_width,
                          y + depthwise_filter_dom[2] - pad_height,
                          b);

        // Convolve the image point-wise: for each pixel we map from
        // input_channels * channel_multiplier number of channels to output_channels
        Func pointwise_convolved("pointwise_convolved");
        RDom pointwise_filter_dom(0, pointwise_filter.dim(1).extent());
        pointwise_convolved(d, x, y, b) = bias(d);
        pointwise_convolved(d, x, y, b) +=
            pointwise_filter(d, pointwise_filter_dom) *
            depthwise_convolved(pointwise_filter_dom, x, y, b);

        // ReLU
        output(d, x, y, b) = max(pointwise_convolved(d, x, y, b), 0.f);

        // The schedule.
        if (auto_schedule) {
            // Second layer of MobileNet v2
            const int N = 4, CI = 32, CO = 16, CM = 1, W = 112, H = 112;

            input.dim(0).set_estimate(0, CI);
            input.dim(1).set_estimate(0, W);
            input.dim(2).set_estimate(0, H);
            input.dim(3).set_estimate(0, N);

            depthwise_filter.dim(0).set_estimate(0, CI / CO);
            depthwise_filter.dim(1).set_estimate(0, CI);
            depthwise_filter.dim(2).set_estimate(0, 3);
            depthwise_filter.dim(3).set_estimate(0, 3);

            pointwise_filter.dim(0).set_estimate(0, CO);
            pointwise_filter.dim(1).set_estimate(0, CI * CM);

            bias.dim(0).set_estimate(0, CO);

            output.dim(0).set_estimate(0, CO);
            output.dim(1).set_estimate(0, W);
            output.dim(2).set_estimate(0, H);
            output.dim(3).set_estimate(0, N);
        } else if (false && get_target().has_gpu_feature()) {
            // 0.100ms on a 2060 RTX super. This is 802 GFlops, which
            // is not a very large fraction of peak flops. For
            // comparison though, tensorflow 2.3 achieves 1.65ms via
            // cudnn 7.

            // We'll do the depthwise convolution and pointwise
            // convolution as two separate kernels. In principle they
            // could be fused, but it's hard to get a compatible
            // thread block and fit everything into registers and
            // shared if you do that.

            Var xi, yi, di, dii, xii, yii;
            RVar ro, ri;

            // The pointwise convolution kernel
            Func(output)
                .tile({d, x, y}, {di, xi, yi}, {16, 4, 4})
                .tile({di, xi, yi}, {dii, xii, yii}, {1, 2, 2})
                .gpu_threads(di, xi, yi)
                .fuse(y, b, b)
                .gpu_blocks(d, x, b)
                .unroll(xii)
                .unroll(yii)
                .unroll(dii);

            pointwise_convolved.compute_at(output, di)
                .reorder(x, y, d)
                .unroll(x)
                .unroll(y)
                .unroll(d)
                .update()
                .unroll(x)
                .unroll(y)
                .unroll(d)
                .split(pointwise_filter_dom, ro, ri, 4)
                .reorder(ri, x, y, d, ro)
                .unroll(ri);

            // We're going to call in() on depthwise_convolved three
            // times! The first will be to give it a compute_root
            // wrapper to do the accumulation in registers. The second
            // will be staging tiles of it into shared in the
            // pointwise kernel. The third will be staging the loads
            // from shared into registers. We write them in reverse order below:

            // We can do 4-wide vectorized loads from shared memory if
            // we unroll the reduction loop by a factor of four above
            // and stage the loads from the depthwise_convolved
            // output.
            depthwise_convolved.in()
                .in()
                .in()
                .compute_at(pointwise_convolved, x)
                .bound_extent(d, 4)
                .vectorize(d)
                .unroll(x)
                .unroll(y);

            // Stage a tile depthwise_convolved into shared memory for
            // each tile of pointwise_convolved.
            depthwise_convolved.in()
                .in()
                .compute_at(output, d)
                .tile({d, x, y}, {di, xi, yi}, {16, 2, 2}, TailStrategy::RoundUp)
                .unroll(xi)
                .unroll(yi)
                .gpu_threads(di, x, y);

            // The depthwise convolution kernel
            depthwise_convolved.in()
                .compute_root()
                .tile({d, x, y}, {di, xi, yi}, {32, 4, 4})
                .tile({di, xi, yi}, {dii, xii, yii}, {2, 2, 2})
                .gpu_threads(di, xi, yi)
                .fuse(y, b, b)
                .gpu_blocks(d, x, b)
                .unroll(xii)
                .unroll(yii)
                .unroll(dii);

            depthwise_convolved
                .compute_at(depthwise_convolved.in(), di)
                .unroll(x)
                .unroll(y)
                .unroll(d)
                .update()
                .reorder(d, x, y, depthwise_filter_dom[1], depthwise_filter_dom[2], depthwise_filter_dom[0])
                .unroll(x)
                .unroll(y)
                .unroll(d);
        } else {
            // CPU schedule

            // 0.146ms on an Intel i9-9960X using 16 threads pinned to 3.0 GHz,
            // which is only about 18% of peak flops.

            int tile_w = 1;
            int tile_h = 1;
            int tile_d = 1;
            const int vec = natural_vector_size<float>();

            // Figure out how many registers we have in the register
            // file on this target.
            int num_regs = 16;
            if (get_target().has_feature(Target::AVX512_Skylake) ||
                (get_target().arch == Target::ARM &&
                 get_target().bits == 64)) {
                num_regs = 32;
            }

            // Pick a tile size designed to fit into the register file.
            if (num_regs == 32 && vec == 16) {
                // 32 vector registers available of size 16. Use 24 of
                // them for accumulators.
                tile_d = 1;
                tile_w = 6;
                tile_h = 4;
                // Using more tiles in the d dimension would be
                // better, but we're tuning for 16 output channels and
                // our vectors are already that wide (on avx512).
            } else if (num_regs == 32 && vec == 4) {
                // 32 vector registers, of size 4. We'll use 24.
                tile_d = 4;
                tile_w = 3;
                tile_h = 2;
            } else if (num_regs == 16 && vec == 8) {
                // 16 registers available of size 8. Use 12 for accumulators.
                tile_d = 2;
                tile_w = 3;
                tile_h = 2;
            } else {
                // Old x86 or 32-bit arm. Assume vectors of size 4,
                // 16 registers. No FMA so we need to reserve a few
                // more registers for things other than the
                // accumulators.
                tile_d = 4;
                tile_w = 2;
                tile_h = 1;
            }
            // Change units from vectors to elements
            tile_d *= vec;

            // We're going to specialize for channel_multiplier = 1,
            // in which case it's nice to know that depthwise_filter
            // is dense across the second dimension.
            depthwise_filter.dim(1).set_stride(channel_multiplier);

            // Unlike in the cuda schedule above, this schedule
            // aggressively fuses the depthwise conv into the
            // pointwise conv. We do the depthwise convolution within
            // slices of the channel reduction loop in the pointwise
            // convolution.

            Var di, xi, yi;
            RVar ro, ri;

            Func(output)
                .tile({d, x, y}, {di, xi, yi}, {tile_d, tile_w, tile_h})
                .vectorize(di)
                .unroll(xi)
                .unroll(yi)
                .fuse(y, b, b)
                .parallel(b);

            pointwise_convolved.compute_at(output, d)
                .vectorize(d)
                .unroll(x)
                .unroll(y)
                .update()
                .reorder(d, x, y, pointwise_filter_dom, b)
                .vectorize(d)
                .unroll(x)
                .unroll(y)
                .split(pointwise_filter_dom, ro, ri, tile_d);

            depthwise_convolved
                .store_in(MemoryType::Stack)
                .bound_extent(d, tile_d)
                .compute_at(pointwise_convolved, ro)
                .vectorize(d)
                .reorder(x, y, d)
                .unroll(x)
                .unroll(y)
                .update()
                .vectorize(d)
                .reorder(x, y, d, depthwise_filter_dom[0], depthwise_filter_dom[1], depthwise_filter_dom[2], b)
                .unroll(x)
                .unroll(y);

            input_bounded
                .store_in(MemoryType::Stack)
                .compute_at(pointwise_convolved, ro)
                .tile(_0, _1, di, xi, vec, 4, TailStrategy::RoundUp)
                .vectorize(di)
                .unroll(xi);

            output.specialize(channel_multiplier == 1);
        }
    }
};
}  // namespace

HALIDE_REGISTER_GENERATOR(DepthwiseSeparableConvolution, depthwise_separable_conv)
