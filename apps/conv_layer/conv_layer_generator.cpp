#include "Halide.h"

namespace {

using namespace Halide;

class ConvolutionLayer : public Halide::Generator<ConvolutionLayer> {
public:
    Input<Buffer<float>> input{"input", 4};
    Input<Buffer<float>> filter{"filter", 4};
    Input<Buffer<float>> bias{"bias", 1};

    Output<Buffer<float>> relu{"relu", 4};

    void generate() {
        const int N = 5, CI = 128, CO = 128, W = 100, H = 80;

        /* THE ALGORITHM */

        Var x("x"), y("y"), c("c"), n("n");

        Func conv("conv");
        RDom r(0, CI, 0, 3, 0, 3);

        conv(c, x, y, n) = bias(c);
        conv(c, x, y, n) += filter(c, r.y, r.z, r.x) * input(r.x, x + r.y, y + r.z, n);

        relu(c, x, y, n) = max(0, conv(c, x, y, n));

        /* THE SCHEDULE */

        // MKL JITs code for the specific size and strides, so we'll
        // do the same and ask Halide to compile for this specific
        // size:
        relu.bound(x, 0, W)
            .bound(y, 0, H)
            .bound(c, 0, CO)
            .bound(n, 0, N);

        relu.dim(0).set_bounds(0, CO).set_stride(1)
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

        input.set_host_alignment(128);
        bias.set_host_alignment(128);
        filter.set_host_alignment(128);
        relu.set_host_alignment(128);

        if (auto_schedule) {
            input.dim(0).set_estimate(0, CI);
            input.dim(1).set_estimate(0, W + 2);
            input.dim(2).set_estimate(0, H + 2);
            input.dim(3).set_estimate(0, N);

            filter.dim(0).set_estimate(0, CO);
            filter.dim(1).set_estimate(0, 3);
            filter.dim(2).set_estimate(0, 3);
            filter.dim(3).set_estimate(0, CI);

            bias.dim(0).set_estimate(0, CO);

            relu.dim(0).set_estimate(0, W);
            relu.dim(1).set_estimate(0, H);
            relu.dim(2).set_estimate(0, CO);
            relu.dim(3).set_estimate(0, N);

        } else if (get_target().has_gpu_feature()) {
            // GPU schedule, tuned for a gtx 980

            // 2.41 ms on a GTX 980. According to nvprof this is about
            // 88% of peak flops.

            Var ni, no, xi, xo, yi, yo, ci, co, t;
            RVar rxo, rxi, rxii;
            relu.compute_root()
                .split(x, xo, xi, 5)
                .split(y, yo, yi, 5)
                .split(c, co, ci, 32)
                .reorder(xi, yi, ci, xo, yo, co, n)
                .gpu_lanes(ci)
                .unroll(xi)
                .unroll(yi)
                .fuse(co, n, t)
                .gpu_blocks(xo, yo, t);

            conv.compute_at(relu, xo)
                .store_in(MemoryType::Register)
                .gpu_lanes(c)
                .unroll(x)
                .unroll(y)
                .update()
                .split(r.x, rxo, rxi, 16)
                .split(rxi, rxi, rxii, 2)
                .reorder(c, rxii, x, y, r.y, r.z, rxi, rxo)
                .gpu_lanes(c)
                .unroll(x)
                .unroll(y)
                .unroll(r.y)
                .unroll(r.z)
                .unroll(rxii);

            input.in().compute_at(conv, rxo)
                .vectorize(_0, 2)
                .split(_1, xo, xi, 4)
                .fuse(_0, xi, t)
                .gpu_lanes(t)
                .unroll(xo)
                .unroll(_2);

        } else {

            int tile_w = 1;
            int tile_h = 1;
            const int vec = natural_vector_size<float>();

            if (get_target().has_feature(Target::AVX512_Skylake) ||
                (get_target().arch == Target::ARM &&
                 get_target().bits == 64)) {
                // On Skylake we have one load per fma and 32
                // registers available, so there's considerable
                // flexibility in the schedule. We'll use 20 accumulator
                // registers in a 4x5 tile. This is also a reasonable
                // choice for ARMv8, which also has 32 registers.
                tile_w = 4;
                tile_h = 5;
            } else if (get_target().arch == Target::X86) {
                // With 16-register ISAs like x86 with AVX2, we can
                // only do one load per two fmas, which constrains the
                // schedule to have to be a squarish 12-register tile
                // of the output.
                tile_w = 3;
                tile_h = 4;
            } else {
                // The above should also be reasonable schedule for
                // ARMv7 and other 16-register machines, but I see
                // some spills on arm-32, so we use a 2x4 block of 8
                // accumulators instead. This could probably be better
                // tuned, because in principle 12 accumulators should
                // be possible. I believe the issue is that there's no
                // fused multiply-add instruction, and so we're
                // fighting llvm's instruction scheduler, which wants
                // to move the muls well ahead of the adds to cover
                // instruction latencies.
                tile_w = 2;
                tile_h = 4;
            }

            Var co, ci, xo, xi, yo, yi, t;
            relu.split(c, co, ci, vec * tile_w)
                .split(x, xo, xi, tile_h)
                .reorder(ci, xi, xo, y, n, co)
                .vectorize(ci, vec)
                .unroll(ci)
                .unroll(xi)
                .parallel(y)
                .parallel(n)
                .parallel(co);
            conv.compute_at(relu, xo)
                .vectorize(c, vec)
                .unroll(c)
                .unroll(x)
                .unroll(y)
                .update()
                .reorder(c, x, y, r.x, r.y, r.z, n)
                .vectorize(c, vec)
                .unroll(c)
                .unroll(x)
                .unroll(y)
                .unroll(r.x, 2);
            filter.in()
                .compute_at(conv, r.x)
                .vectorize(_0, vec)
                .unroll(_0)
                .unroll(_3);
            input.in()
                .compute_at(conv, x)
                .unroll(_0);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ConvolutionLayer, conv_layer)
