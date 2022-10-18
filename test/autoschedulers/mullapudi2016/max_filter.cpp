// Circular-support max filter. Does some trickery to get O(r) per pixel for radius r, not O(r^2).

#include "Halide.h"
#include "halide_benchmark.h"
#include <iostream>
#include <limits>

using namespace Halide;
using namespace Halide::Tools;

double run_test(bool auto_schedule) {
    int W = 1920;
    int H = 1024;
    Buffer<float> in(W, H, 3);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            for (int c = 0; c < 3; c++) {
                in(x, y, c) = rand() & 0xfff;
            }
        }
    }

    const int radius = 26;

    Func input = BoundaryConditions::repeat_edge(in);

    Var x, y, c, t;

    const int slices = (int)(ceilf(logf(radius) / logf(2))) + 1;

    // A sequence of vertically-max-filtered versions of the input,
    // each filtered twice as tall as the previous slice. All filters
    // are downward-looking.
    Func vert_log;
    vert_log(x, y, c, t) = input(x, y, c);
    RDom r(-radius, in.height() + radius, 1, slices - 1);
    vert_log(x, r.x, c, r.y) = max(vert_log(x, r.x, c, r.y - 1),
                                   vert_log(x, r.x + clamp((1 << cast<uint32_t>(r.y - 1)), 0, radius * 2), c, r.y - 1));

    // We're going to take a max filter of arbitrary diameter
    // by maxing two samples from its floor log 2 (e.g. maxing two
    // 8-high overlapping samples). This next Func tells us which
    // slice to draw from for a given radius:
    Func slice_for_radius;
    slice_for_radius(t) = cast<int>(floor(log(2 * t + 1) / logf(2)));

    // Produce every possible vertically-max-filtered version of the image:
    Func vert;
    // t is the blur radius
    Expr slice = clamp(slice_for_radius(t), 0, slices);
    Expr first_sample = vert_log(x, y - t, c, slice);
    Expr second_sample = vert_log(x, y + t + 1 - clamp(1 << cast<uint32_t>(slice), 0, 2 * radius), c, slice);
    vert(x, y, c, t) = max(first_sample, second_sample);

    Func filter_height;
    RDom dy(0, radius + 1);
    filter_height(x) = sum(select(x * x + dy * dy < (radius + 0.25f) * (radius + 0.25f), 1, 0));

    // Now take an appropriate horizontal max of them at each output pixel
    Func final;
    RDom dx(-radius, 2 * radius + 1);
    final(x, y, c) = maximum(vert(x + dx, y, c, clamp(filter_height(dx), 0, radius + 1)));

    Target target = get_jit_target_from_environment();
    Pipeline p(final);

    Var tx, xi;
    if (auto_schedule) {
        // Provide estimates on the pipeline output
        final.set_estimate(x, 0, in.width())
            .set_estimate(y, 0, in.height())
            .set_estimate(c, 0, in.channels());
        // Auto-schedule the pipeline
        p.apply_autoscheduler(target, {"Mullapudi2016"});
    } else if (target.has_gpu_feature()) {
        slice_for_radius.compute_root();
        filter_height.compute_root();
        Var xo, yi;

        final
            .split(x, xo, xi, 128)
            .reorder(xi, xo, y, c)
            .gpu_blocks(xo, y, c)
            .gpu_threads(xi);

        vert_log.compute_root()
            .reorder(c, t, x, y)
            .gpu_tile(x, y, xi, yi, 16, 16)
            .update()
            .split(x, xo, xi, 128)
            .reorder(r.x, r.y, xi, xo, c)
            .gpu_blocks(xo, c)
            .gpu_threads(xi);
    } else {
        // These don't matter, just LUTs
        slice_for_radius.compute_root();
        filter_height.compute_root();

        // vert_log.update(1) doesn't have enough parallelism, but I
        // can't figure out how to give it more... Split whole image
        // into slices.

        final.compute_root().split(x, tx, x, 256).reorder(x, y, c, tx).fuse(c, tx, t).parallel(t).vectorize(x, 8);
        vert_log.compute_at(final, t);
        vert_log.vectorize(x, 8);
        vert_log.update().reorder(x, r.x, r.y, c).vectorize(x, 8);
        vert.compute_at(final, y).vectorize(x, 8);
    }

    p.compile_to_lowered_stmt("max_filter.html", {in}, HTML, target);
    // Inspect the schedule (only for debugging))
    // final.print_loop_nest();

    // Run the schedule
    Buffer<float> out(in.width(), in.height(), in.channels());
    double time = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return time * 1000;
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

    const double slowdown_factor = 4.0;
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
