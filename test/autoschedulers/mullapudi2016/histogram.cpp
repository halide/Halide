#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

double run_test(bool auto_schedule) {
    int W = 1920;
    int H = 1024;
    Buffer<uint8_t> in(W, H, 3);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            for (int c = 0; c < 3; c++) {
                in(x, y, c) = rand() & 0xfff;
            }
        }
    }

    Var x("x"), y("y"), c("c");

    Func Y("Y");
    Y(x, y) = 0.299f * in(x, y, 0) + 0.587f * in(x, y, 1) + 0.114f * in(x, y, 2);

    Func Cr("Cr");
    Expr R = in(x, y, 0);
    Cr(x, y) = (R - Y(x, y)) * 0.713f + 128;

    Func Cb("Cb");
    Expr B = in(x, y, 2);
    Cb(x, y) = (B - Y(x, y)) * 0.564f + 128;

    Func hist_rows("hist_rows");
    hist_rows(x, y) = 0;
    RDom rx(0, in.width());
    Expr bin = cast<uint8_t>(clamp(Y(rx, y), 0, 255));
    hist_rows(bin, y) += 1;

    Func hist("hist");
    hist(x) = 0;
    RDom ry(0, in.height());
    hist(x) += hist_rows(x, ry);

    Func cdf("cdf");
    cdf(x) = hist(0);
    RDom b(1, 255);
    cdf(b.x) = cdf(b.x - 1) + hist(b.x);

    Func eq("equalize");

    Expr cdf_bin = cast<uint8_t>(clamp(Y(x, y), 0, 255));
    eq(x, y) = clamp(cdf(cdf_bin) * (255.0f / (in.height() * in.width())), 0, 255);

    Func color("color");
    Expr red = cast<uint8_t>(clamp(eq(x, y) + (Cr(x, y) - 128) * 1.4f, 0, 255));
    Expr green = cast<uint8_t>(clamp(eq(x, y) - 0.343f * (Cb(x, y) - 128) - 0.711f * (Cr(x, y) - 128), 0, 255));
    Expr blue = cast<uint8_t>(clamp(eq(x, y) + 1.765f * (Cb(x, y) - 128), 0, 255));
    color(x, y, c) = mux(c, {red, green, blue});

    Target target = get_jit_target_from_environment();
    Pipeline p(color);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        color.set_estimates({{0, 1920}, {0, 1024}, {0, 3}});
        // Auto-schedule the pipeline
        p.apply_autoscheduler(target, {"Mullapudi2016"});
    } else if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi");
        Y.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
        hist_rows.compute_root().gpu_tile(y, yi, 16).update().gpu_tile(y, yi, 16);
        hist.compute_root().gpu_tile(x, xi, 16).update().gpu_tile(x, xi, 16);
        cdf.compute_root().gpu_single_thread();
        Cr.compute_at(color, xi);
        Cb.compute_at(color, xi);
        eq.compute_at(color, xi);
        color.compute_root()
            .reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c)
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
        eq.compute_at(color, x).unroll(x);
        Cb.compute_at(color, x).vectorize(x);
        Cr.compute_at(color, x).vectorize(x);
        color.reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c)
            .parallel(y, 8)
            .vectorize(x, 8);
    }

    p.compile_to_lowered_stmt("histogram.html", {in}, HTML, target);
    // Inspect the schedule (only for debugging))
    // color.print_loop_nest();

    Buffer<uint8_t> out(in.width(), in.height(), in.channels());
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t * 1000;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Autoschedulers do not support WebAssembly.\n");
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }

    load_plugin(argv[1]);

    double manual_time = run_test(false);
    double auto_time = run_test(true);

    const double slowdown_factor = 5.0;
    if (!get_jit_target_from_environment().has_gpu_feature() && auto_time > manual_time * slowdown_factor) {
        std::cerr << "Autoscheduler time is slower than expected:\n"
                  << "======================\n"
                  << "Manual time: " << manual_time << "ms\n"
                  << "Auto time: " << auto_time << "ms\n"
                  << "======================\n";
        exit(1);
    }

    printf("Success!\n");
    return 0;
}
