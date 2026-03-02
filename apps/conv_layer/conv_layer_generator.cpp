#include "Halide.h"

namespace {

using namespace Halide;

class ConvolutionLayer : public Halide::Generator<ConvolutionLayer> {
public:
    Input<Buffer<float, 4>> input{"input"};
    Input<Buffer<float, 4>> filter{"filter"};
    Input<Buffer<float, 1>> bias{"bias"};
    Input<Buffer<float, 1>> alpha{"alpha"};
    Output<Buffer<float, 4>> prelu{"prelu"};

    void generate() {
        // Use MAX_W/MAX_H as the maximum expected spatial size (<= 272 at runtime)
        const int N = 1, CI = 4, CO = 4, MAX_H = 272, MAX_W = 272;

        /* THE ALGORITHM */

        Var x("x"), y("y"), c("c"), n("n");

        // Create a zero-padded view of `input` (constant exterior = 0)
        Func input_padded = BoundaryConditions::constant_exterior(input, 0.0f);

        Func conv("conv");
        RDom r(0, CI, 0, 3, 0, 3);

        // Use an offset of -1 to map the 3x3 kernel to "same" padding
        conv(c, x, y, n) = bias(c);
        conv(c, x, y, n) += filter(c, r.y, r.z, r.x) *
                    input_padded(r.x, x + r.y - 1, y + r.z - 1, n);

        // PReLU: output = conv >= 0 ? conv : alpha(c) * conv
        prelu(c, x, y, n) = select(conv(c, x, y, n) >= 0,
                       conv(c, x, y, n),
                       alpha(c) * conv(c, x, y, n));

        /* THE SCHEDULE */

        // MKL JITs code for the specific size and strides, so we'll
        // do the same and ask Halide to compile for this specific
        // size:

        prelu.dim(0).set_bounds(0, CO).set_stride(1);
        prelu.dim(1).set_stride(CO);
        prelu.dim(3).set_bounds(0, N);

        // With "same" padding the input spatial size equals the output
        // Keep channel dim fixed, but allow spatial dims to vary at runtime
        input.dim(0).set_bounds(0, CI).set_stride(1);
        input.dim(1).set_stride(CI);
        // Intentionally do NOT set bounds/strides for input.dim(1) and input.dim(2)
        // so that width and height may vary at runtime.
        input.dim(3).set_bounds(0, N);

        filter.dim(0).set_bounds(0, CO).set_stride(1);
        filter.dim(1).set_bounds(0, 3).set_stride(CO);
        filter.dim(2).set_bounds(0, 3).set_stride(CO * 3);
        filter.dim(3).set_bounds(0, CI).set_stride(CO * 3 * 3);

        bias.dim(0).set_bounds(0, CO).set_stride(1);
        alpha.dim(0).set_bounds(0, CO).set_stride(1);

        if (using_autoscheduler()) {
            // Provide the autoscheduler with conservative estimates (max expected sizes)
            input.dim(0).set_estimate(0, CI);
            input.dim(1).set_estimate(0, MAX_W);
            input.dim(2).set_estimate(0, MAX_H);
            input.dim(3).set_estimate(0, N);

            filter.dim(0).set_estimate(0, CO);
            filter.dim(1).set_estimate(0, 3);
            filter.dim(2).set_estimate(0, 3);
            filter.dim(3).set_estimate(0, CI);

            bias.dim(0).set_estimate(0, CO);
            alpha.dim(0).set_estimate(0, CO);

            // prelu dims: 0=channels(CO), 1=width, 2=height, 3=batch
            prelu.dim(0).set_estimate(0, CO);
            prelu.dim(1).set_estimate(0, MAX_W);
            prelu.dim(2).set_estimate(0, MAX_H);
            prelu.dim(3).set_estimate(0, N);

        } else if (get_target().has_feature(Target::CUDA)) {
            // GPU schedule, tuned for a GTX 980. Seems to be good on
            // an RTX 2060 too (About 90% peak flops on both cards).

            // 1.87 ms on an RTX 2060. According to NVIDIA Nsight
            // Compute we're at 91.5% utilization of the FMA units

            // 2.41 ms on a GTX 980. According to nvprof this is about
            // 88% of peak flops.

            // We use cuda-specific scheduling directives (gpu_lanes),
            // so this is not a general GPGPU schedule.

            Var ni, no, xi, xo, yi, yo, ci, co, t;
            RVar rxo, rxi, rxii;
            prelu.compute_root()
                .split(x, xo, xi, 5)
                .split(y, yo, yi, 5)
                .split(c, co, ci, 32)
                .reorder(xi, yi, ci, xo, yo, co, n)
                .gpu_lanes(ci)
                .unroll(xi)
                .unroll(yi)
                .fuse(co, n, t)
                .gpu_blocks(xo, yo, t);

            conv.compute_at(prelu, xo)
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

            input.in()
                .compute_at(conv, rxo)
                .vectorize(_0, 2)
                .split(_1, xo, xi, 4)
                .fuse(_0, xi, t)
                .gpu_lanes(t)
                .unroll(xo)
                .unroll(_2);
            
            std::cout << "Using GPU schedule" << std::endl;
            std::cout << "loop nest:" << std::endl;
            prelu.print_loop_nest();

        } else {

            // 4.06ms on an Intel i9-9960X using 16 threads at 3.0 GHz,
            // which is 94.5% of peak flops assuming the math below is correct:

            // 16 cores times 2 FMAs per cycle times 3G cycles per
            // second times 16 vector lanes is a peak throughput of
            // 1.536 TFlops.

            // This conv does N * CI * CO * W * H * 3 * 3 = 5 * 128 *
            // 128 * 100 * 80 * 3 * 3 FMAs in 4.06ms is 1.453 TFlops.

            // The ratio of actual to theoretical flops hit is 0.9458

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

            Var co("co"), ci("ci"), xo("xo"), xi("xi"), yo("yo"), yi("yi"), t("t");
            prelu.split(c, co, ci, tile_w)
                .split(x, xo, xi, tile_h)
                .reorder(ci, xi, xo, y, n, co)
                .vectorize(ci, vec)
                .unroll(ci)
                .unroll(xi)
                .parallel(y)
                .parallel(n)
                .parallel(co);
            conv.compute_at(prelu, xo)
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

            std::cout << "Using CPU schedule" << std::endl;
            std::cout << "tile size: " << tile_w << " x " << tile_h << std::endl;
            std::cout << "vector size: " << vec << std::endl;
            std::cout << "loop nest:" << std::endl;
            prelu.print_loop_nest();
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ConvolutionLayer, conv_layer)
