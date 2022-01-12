#include "Halide.h"

namespace {

using namespace Halide;
using namespace Halide::BoundaryConditions;

class DepthwiseSeparableConvolution : public Generator<DepthwiseSeparableConvolution> {
public:
    // [in_channels, width, height, batch_size]
    Input<Buffer<float, 4>> input{"input"};

    // [channel_multiplier, in_channels, filter_width, filter_height]
    Input<Buffer<float, 4>> depthwise_filter{"depthwise_filter"};

    // [out_channels, channel_multiplier * in_channels]
    Input<Buffer<float, 2>> pointwise_filter{"pointwise_filter"};

    // [out_channels]
    Input<Buffer<float, 1>> bias{"bias"};

    // [out_channels, width, height, batch_size]
    Output<Buffer<float, 4>> output{"output"};

    void generate() {
        // The algorithm. It will be a generic depthwise convolution,
        // with no assumptions about input sizes or shapes. This makes
        // it especially challenging to schedule.

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), d("d"), b("b");

        // Pad x and y with 0. Unfortunately the built-in boundary
        // condition helpers cause unwanted loop partitioning.
        Func input_bounded;
        Expr in_bounds = (x >= 0 && x < input.dim(1).extent() &&
                          y >= 0 && y < input.dim(2).extent());
        Expr clamped_x = clamp(x, 0, input.dim(1).max());
        Expr clamped_y = clamp(y, 0, input.dim(2).max());
        input_bounded(d, x, y, b) =
            select(in_bounds, input(d, clamped_x, clamped_y, b), 0.0f);

        Expr channel_multiplier = depthwise_filter.dim(0).extent();

        // Convolve the image depthwise -- for each input channel,
        // generate channel_multiplier number of intermediate channels using convolution
        Func depthwise_convolved("depthwise_convolved");
        Expr pad_width = depthwise_filter.dim(2).extent() / 2;
        Expr pad_height = depthwise_filter.dim(3).extent() / 2;
        RDom depthwise_filter_dom(0, depthwise_filter.dim(0).extent(),
                                  0, depthwise_filter.dim(2).extent(),
                                  0, depthwise_filter.dim(3).extent());
        // Give clearer names to the reduction over input channels (depth), x and y.
        RVar rd = depthwise_filter_dom[0];
        RVar rx = depthwise_filter_dom[1];
        RVar ry = depthwise_filter_dom[2];
        depthwise_convolved(d, x, y, b) +=
            depthwise_filter(rd, d, rx, ry) *
            input_bounded(d / channel_multiplier,
                          x + rx - pad_width,
                          y + ry - pad_height,
                          b);

        // Convolve the image point-wise: for each pixel we map from
        // input_channels * channel_multiplier number of channels to output_channels
        Func pointwise_convolved("pointwise_convolved");
        // Reduction over the channels in the depthwise output
        RDom rc(0, pointwise_filter.dim(1).extent());
        pointwise_convolved(d, x, y, b) = bias(d);
        pointwise_convolved(d, x, y, b) +=
            pointwise_filter(d, rc) * depthwise_convolved(rc, x, y, b);

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
        } else if (get_target().has_gpu_feature()) {
            // 0.066ms on a 2060 RTX super. This is about 1.2 TFlops,
            // which is not a very large fraction of peak. For
            // comparison though, tensorflow 2.3 achieves 0.13ms via
            // cudnn 7. So we're twice as fast.

            // This schedule fuses the depthwise conv into the pointwise
            // conv. The results of the depthwise conv are computed inside
            // the outer of the two pointwise reduction loops.

            Var xi, yi, di, dii, xii, yii;
            RVar ro, ri;

            // The pointwise convolution kernel. Produces a 4x4 tile of output.
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
                .split(rc, ro, ri, 4)
                .reorder(ri, x, y, d, ro)
                .unroll(ri);

            // We're going to call in() on depthwise_convolved twice.
            // The first will be to give it a wrapper to do the
            // accumulation in registers before writing the result to
            // shared. The second will be staging the loads from
            // shared into registers. We write them in reverse order
            // below:

            // We can do 4-wide vectorized loads from shared memory if
            // we unroll the reduction loop by a factor of four above
            // and stage the loads from the depthwise_convolved
            // output.

            depthwise_convolved.in()
                .in()
                .compute_at(pointwise_convolved, x)
                .bound_extent(d, 4)
                .vectorize(d)
                .unroll(x)
                .unroll(y);

            // The depthwise convolution kernel. Produces a 4x4 tile
            // of intermediate state, storing the result in shared.
            depthwise_convolved.in()
                .compute_at(output, d)
                .tile({d, x, y}, {di, xi, yi}, {32, 4, 4}, TailStrategy::RoundUp)
                .tile({di, xi, yi}, {dii, xii, yii}, {2, 2, 2})
                .gpu_threads(di, xi, yi)
                .unroll(xii)
                .unroll(yii)
                .unroll(dii);

            depthwise_convolved
                .compute_at(depthwise_convolved.in(), di)
                .unroll(x)
                .unroll(y)
                .unroll(d)
                .update()
                .reorder(d, x, y, rx, ry, rd)
                .unroll(x)
                .unroll(y)
                .unroll(d);
        } else {
            // CPU schedule

            // 0.13ms on an Intel i9-9960X using 16 threads pinned to 3.0 GHz,
            // which is only about 20% of peak flops.

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

            // This schedule aggressively fuses the depthwise conv into
            // the pointwise conv. We do the depthwise convolution within
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
                .reorder(d, x, y, rc, b)
                .vectorize(d)
                .unroll(x)
                .unroll(y)
                .split(rc, ro, ri, tile_d);

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
                .reorder(x, y, d, rd, rx, ry, b)
                .unroll(x)
                .unroll(y);

            input_bounded
                .store_in(MemoryType::Stack)
                .compute_at(pointwise_convolved, ro)
                .tile(d, x, di, xi, vec, 4, TailStrategy::RoundUp)
                .vectorize(di)
                .unroll(xi);
        }

        if (!auto_schedule) {
            // We're going to specialize both schedules for channel_multiplier = 1,
            // in which case it's nice to know that depthwise_filter
            // is dense across the second dimension.
            depthwise_filter.dim(1).set_stride(channel_multiplier);
            Expr intermediate_channels = pointwise_filter.dim(1).extent();
            // We'll also specialize for a multiple-of-32 intermediate
            // channels, and a 3x3 conv.
            output.specialize(channel_multiplier == 1 &&
                              intermediate_channels == (intermediate_channels / 32) * 32 &&
                              depthwise_filter.dim(2).extent() == 3 &&
                              depthwise_filter.dim(3).extent() == 3);
        }
    }
};
}  // namespace

HALIDE_REGISTER_GENERATOR(DepthwiseSeparableConvolution, depthwise_separable_conv)
