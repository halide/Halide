#include "Halide.h"

namespace {

using namespace Halide::ConciseCasts;

class Hist : public Halide::Generator<Hist> {
public:
    Input<Buffer<uint8_t>>  input{"input", 3};
    Output<Buffer<uint8_t>> output{"output", 3};

    void generate() {
        Var x("x"), y("y"), c("c");

        // Algorithm
        Func Y("Y");
        Y(x, y) = 0.299f * input(x, y, 0) + 0.587f * input(x, y, 1)
                  + 0.114f * input(x, y, 2);

        Func Cr("Cr");
        Expr R = input(x, y, 0);
        Cr(x, y) = (R - Y(x, y)) * 0.713f + 128;

        Func Cb("Cb");
        Expr B = input(x, y, 2);
        Cb(x, y) = (B - Y(x, y)) * 0.564f + 128;

        Func hist_rows("hist_rows");
        hist_rows(x, y) = 0;
        RDom rx(0, 1536);
        Expr bin = u8(clamp(Y(rx, y), 0, 255));
        hist_rows(bin, y) += 1;

        Func hist("hist");
        hist(x) = 0;
        RDom ry(0, 2560);
        hist(x) += hist_rows(x, ry);

        Func cdf("cdf");
        cdf(x) = hist(0);
        RDom b(1, 255);
        cdf(b.x) = cdf(b.x - 1) + hist(b.x);

        Func eq("equalize");

        Expr cdf_bin = u8(clamp(Y(x, y), 0 , 255));
        eq(x, y) = clamp(cdf(cdf_bin) * (255.0f/(input.height() * input.width())), 0 , 255);

        Expr red = u8(clamp(eq(x, y) + (Cr(x, y) - 128) * 1.4f, 0, 255));
        Expr green = u8(clamp(eq(x, y) - 0.343f * (Cb(x, y) - 128) - 0.711f * (Cr(x, y) -128), 0, 255));
        Expr blue = u8(clamp(eq(x, y) + 1.765f * (Cb(x, y) - 128), 0, 255));
        output(x, y, c) = select(c == 0, red,
                                 c == 1, green,
                                         blue);


        // Estimates (for autoscheduler; ignored otherwise)
        {
            input.dim(0).set_bounds_estimate(0, 1536)
                 .dim(1).set_bounds_estimate(0, 2560)
                 .dim(2).set_bounds_estimate(0, 3);
            output.dim(0).set_bounds_estimate(0, 1536)
                  .dim(1).set_bounds_estimate(0, 2560)
                  .dim(2).set_bounds_estimate(0, 3);
        }

        // Schedule
        if (!auto_schedule) {
            cdf.bound(x, 0, 256);

            Var xi("xi"), yi("yi");
            if (get_target().has_gpu_feature()) {
                Y.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
                hist_rows.compute_root().gpu_tile(y, yi, 16).update().gpu_tile(y, yi, 16);
                hist.compute_root().gpu_tile(x, xi, 16).update().gpu_tile(x, xi, 16);
                cdf.compute_root().gpu_single_thread();
                Cr.compute_at(output, x);
                Cb.compute_at(output, x);
                eq.compute_at(output, x);
                output.compute_root()
                      .reorder(c, x, y).bound(c, 0, 3).unroll(c)
                      .gpu_tile(x, y, xi, yi, 16, 16);
            } else {
                Y.compute_root().parallel(y, 8).vectorize(x, 8);
                hist_rows.compute_root()
                    .vectorize(x, 8)
                    .parallel(y, 8)
                    .update()
                    .parallel(y, 8);
                hist.compute_root()
                    .vectorize(x, 8)
                    .update()
                    .reorder(x, ry)
                    .vectorize(x, 8)
                    .unroll(x, 4)
                    .parallel(x)
                    .reorder(ry, x);

                cdf.compute_root();
                eq.compute_at(output, x).unroll(x);
                Cb.compute_at(output, x).vectorize(x);
                Cr.compute_at(output, x).vectorize(x);
                output.reorder(c, x, y)
                      .bound(c, 0, 3)
                      .unroll(c)
                      .parallel(y, 8)
                      .vectorize(x, 8);
            }
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Hist, hist)
