#include "Halide.h"

namespace {

using namespace Halide::ConciseCasts;

class Hist : public Halide::Generator<Hist> {
public:
    Input<Buffer<uint8_t, 3>> input{"input"};
    Output<Buffer<uint8_t, 3>> output{"output"};

    void generate() {
        Var x("x"), y("y"), c("c");

        // Algorithm
        Func Y("Y");
        Y(x, y) = (0.299f * input(x, y, 0) +
                   0.587f * input(x, y, 1) +
                   0.114f * input(x, y, 2));

        Func Cr("Cr");
        Expr R = input(x, y, 0);
        Cr(x, y) = (R - Y(x, y)) * 0.713f + 128;

        Func Cb("Cb");
        Expr B = input(x, y, 2);
        Cb(x, y) = (B - Y(x, y)) * 0.564f + 128;

        Func hist_rows("hist_rows");
        hist_rows(x, y) = 0;
        RDom rx(0, input.width());
        Expr bin = cast<int>(clamp(Y(rx, y), 0, 255));
        hist_rows(bin, y) += 1;

        Func hist("hist");
        hist(x) = 0;
        RDom ry(0, input.height());
        hist(x) += hist_rows(x, ry);

        Func cdf("cdf");
        cdf(x) = hist(0);
        RDom b(1, 255);
        cdf(b.x) = cdf(b.x - 1) + hist(b.x);

        Func cdf_bin("cdf_bin");
        cdf_bin(x, y) = u8(clamp(Y(x, y), 0, 255));

        Func eq("equalize");
        eq(x, y) = clamp(cdf(cdf_bin(x, y)) * (255.0f / (input.height() * input.width())), 0, 255);

        Expr red = u8(clamp(eq(x, y) + (Cr(x, y) - 128) * 1.4f, 0, 255));
        Expr green = u8(clamp(eq(x, y) - 0.343f * (Cb(x, y) - 128) - 0.711f * (Cr(x, y) - 128), 0, 255));
        Expr blue = u8(clamp(eq(x, y) + 1.765f * (Cb(x, y) - 128), 0, 255));
        output(x, y, c) = mux(c, {red, green, blue});

        // Estimates (for autoscheduler; ignored otherwise)
        {
            input.dim(0).set_estimate(0, 1536);
            input.dim(1).set_estimate(0, 2560);
            input.dim(2).set_estimate(0, 3);
            output.dim(0).set_estimate(0, 1536);
            output.dim(1).set_estimate(0, 2560);
            output.dim(2).set_estimate(0, 3);
        }

        // Schedule
        if (!auto_schedule) {
            cdf.bound(x, 0, 256);

            Var xi("xi"), yi("yi");
            if (get_target().has_gpu_feature()) {
                // 0.197ms on a 2060 RTX
                Var yii;
                RVar rxo, rxi;

                if (get_target().has_feature(Target::CUDA)) {
                    // Each thread below will use atomic integer adds
                    // to shared to compute the histogram of a single
                    // row.
                    hist_rows
                        .in()
                        .compute_root()
                        .split(x, x, xi, 64)
                        .vectorize(xi, 2)
                        .unroll(x)
                        .gpu_lanes(xi)
                        .gpu_blocks(y);

                    hist_rows
                        .store_in(MemoryType::GPUShared)
                        .compute_at(hist_rows.in(), y)
                        .split(x, x, xi, 64)
                        .vectorize(xi, 2)
                        .unroll(x)
                        .gpu_lanes(xi)
                        .update()
                        .split(rx, rxo, rxi, 32)
                        .reorder(rxi, rxo, y)
                        .atomic()
                        .gpu_lanes(rxi);

                    Y.clone_in(hist_rows)
                        .compute_at(hist_rows, rxo)
                        .store_in(MemoryType::Register)
                        .gpu_lanes(x);
                } else {
                    hist_rows.compute_root()
                        .gpu_tile(x, y, xi, yi, 32, 8);

                    const int slice_width = 256;
                    // Get more parallelism by not just taking
                    // histograms of rows, but histograms of small
                    // pieces of each row.
                    hist_rows.update()
                        .split(rx, rxo, rxi, slice_width);
                    Var z, zi;
                    Func intm = hist_rows.update().rfactor(rxo, z);

                    intm.in()
                        .compute_root()
                        .gpu_tile(y, z, yi, zi, 16, 1);

                    intm.compute_at(intm.in(), y)
                        .split(x, x, xi, 16)
                        .gpu_threads(xi)
                        .update()
                        .gpu_threads(y);

                    // hist_rows now just sums up the mini-histograms
                    // along the z dimension.
                    hist_rows.update().gpu_tile(x, y, xi, yi, 32, 8);

                    if (!get_target().has_feature(Target::Metal) &&
                        !get_target().has_feature(Target::D3D12Compute)) {
                        // bound_extent doesn't currently work inside
                        // metal & d3d12compute kernels because we can't compile the
                        // assertion. For metal & d3d12compute we just inline the
                        // luma computation.
                        Y.clone_in(intm)
                            .compute_at(intm.in(), y)
                            .split(x, x, xi, 16)
                            .bound_extent(x, 16)
                            .gpu_threads(xi);
                    }
                }
                hist.compute_root()
                    .gpu_tile(x, xi, 16)
                    .update()
                    .gpu_tile(x, xi, 16);
                cdf.compute_root()
                    .gpu_tile(x, xi, 16)
                    .update()
                    .gpu_single_thread();
                output.compute_root()
                    .reorder(c, x, y)
                    .bound(c, 0, 3)
                    .unroll(c)
                    .gpu_tile(x, y, xi, yi, 128, 4)
                    .vectorize(xi, 4);
                Cb.compute_at(output, xi).vectorize(x);
                Cr.compute_at(output, xi).vectorize(x);
                eq.compute_at(output, xi).vectorize(x);
                // Stage the LUT into shared memory
                cdf.in()
                    .compute_at(output, x)
                    .split(x, x, xi, 64)
                    .vectorize(xi, 2)
                    .gpu_threads(xi, x);
            } else {
                // Runtime is noisy. 0.8ms - 1.1ms on an Intel
                // i9-9960X using 16 threads

                const int vec = natural_vector_size<float>();
                // Make separate copies of Y to use while
                // histogramming and while computing the output. It's
                // better to redundantly luminance than reload it, but
                // you don't want to inline it into the histogram
                // computation because then it doesn't vectorize.
                RVar rxo, rxi;
                Y.clone_in(hist_rows)
                    .compute_at(hist_rows.in(), y)
                    .vectorize(x, vec);

                hist_rows.in()
                    .compute_root()
                    .vectorize(x, vec)
                    .parallel(y, 4);
                hist_rows.compute_at(hist_rows.in(), y)
                    .vectorize(x, vec)
                    .update()
                    .reorder(y, rx)
                    .unroll(y);
                hist.compute_root()
                    .vectorize(x, vec)
                    .update()
                    .reorder(x, ry)
                    .vectorize(x, vec)
                    .unroll(x, 4)
                    .parallel(x)
                    .reorder(ry, x);

                cdf.compute_root();
                output.reorder(c, x, y)
                    .bound(c, 0, 3)
                    .unroll(c)
                    .parallel(y, 8)
                    .vectorize(x, vec * 2);
            }
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Hist, hist)
