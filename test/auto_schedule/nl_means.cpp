#include "Halide.h"
#include "benchmark.h"

using namespace Halide;

double run_test(bool auto_schedule) {
    // This implements the basic description of non-local means found at
    // https://en.wikipedia.org/wiki/Non-local_means.

    int patch_size = 7;
    int search_area = 7;

    int H = 1024;
    int W = 500;
    Image<float> input(H, W, 3);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            for (int c = 0; c < 3; c++) {
                input(x, y, c) = rand() & 0xfff;
            }
        }
    }

    float sigma = 0.12f;
    Var x("x"), y("y"), z("z"), c("c");

    Expr inv_sigma_sq = -1.0f/(sigma*sigma*patch_size*patch_size);

    // Add a boundary condition
    Func clamped = BoundaryConditions::repeat_edge(input);

    // Define the difference images.
    Var dx("dx"), dy("dy");
    Func dc("d");
    dc(x, y, dx, dy, c) = pow(clamped(x, y, c) - clamped(x + dx, y + dy, c), 2);

    // Sum across color channels
    RDom channels(0, 3);
    Func d("d");
    d(x, y, dx, dy) = sum(dc(x, y, dx, dy, channels));

    // Find the patch differences by blurring the difference images.
    RDom patch_dom(-patch_size/2, patch_size);
    Func blur_d_y("blur_d_y");
    blur_d_y(x, y, dx, dy) = sum(d(x, y + patch_dom, dx, dy));

    Func blur_d("blur_d");
    blur_d(x, y, dx, dy) = sum(blur_d_y(x + patch_dom, y, dx, dy));

    // Compute the weights from the patch differences.
    Func w("w");
    w(x, y, dx, dy) = fast_exp(blur_d(x, y, dx, dy)*inv_sigma_sq);

    // Add an alpha channel
    Func clamped_with_alpha;
    clamped_with_alpha(x, y, c) = select(c == 0, clamped(x, y, 0),
                                         c == 1, clamped(x, y, 1),
                                         c == 2, clamped(x, y, 2),
                                         1.0f);

    // Define a reduction domain for the search area.
    RDom s_dom(-search_area/2, search_area, -search_area/2, search_area);

    // Compute the sum of the pixels in the search area.
    Func non_local_means_sum("non_local_means_sum");
    non_local_means_sum(x, y, c) += w(x, y, s_dom.x, s_dom.y) * clamped_with_alpha(x + s_dom.x, y + s_dom.y, c);

    Func non_local_means("non_local_means");
    non_local_means(x, y, c) =
            clamp(non_local_means_sum(x, y, c) / non_local_means_sum(x, y, 3), 0.0f, 1.0f);

    // Require 3 channels for output.
    non_local_means.output_buffer().set_min(2, 0).set_extent(2, 3);

    non_local_means.estimate(x, 0, input.width()).estimate(y, 0, input.height()).estimate(c, 0, 3);

    Var tx("tx"), ty("ty"), xi, yi;

    // Schedule.
    Target target = get_target_from_environment();
    Pipeline p(non_local_means);

    if (!auto_schedule) {
        if (target.has_gpu_feature()) {
            non_local_means.compute_root()
                    .reorder(c, x, y).unroll(c)
                    .gpu_tile(x, y, 16, 8);

            d.compute_at(non_local_means_sum, s_dom.x)
                    .tile(x, y, xi, yi, 2, 2)
                    .unroll(xi)
                    .unroll(yi)
                    .gpu_threads(x, y);

            blur_d_y.compute_at(non_local_means_sum, s_dom.x)
                    .unroll(x, 2).gpu_threads(x, y);

            blur_d.compute_at(non_local_means_sum, s_dom.x)
                    .gpu_threads(x, y);

            non_local_means_sum.compute_at(non_local_means, Var::gpu_blocks())
                    .gpu_threads(x, y)
                    .update()
                    .reorder(x, y, c, s_dom.x, s_dom.y)
                    .gpu_threads(x, y);

        } else {
            non_local_means.compute_root()
                    .reorder(c, x, y)
                    .tile(x, y, tx, ty, x, y, 16, 8)
                    .parallel(ty)
                    .vectorize(x, 8);

            blur_d_y.compute_at(non_local_means, tx)
                    .reorder(y, x)
                    .vectorize(x, 8);
            d.compute_at(non_local_means, tx)
                    .vectorize(x, 8);
            non_local_means_sum.compute_at(non_local_means, x)
                    .reorder(c, x, y)
                    .bound(c, 0, 4).unroll(c)
                    .vectorize(x, 8);
            non_local_means_sum.update(0)
                    .reorder(c, x, y, s_dom.x, s_dom.y)
                    .unroll(c)
                    .vectorize(x, 8);
            blur_d.compute_at(non_local_means_sum, x)
                    .vectorize(x, 8);
        }
    } else {
        // Auto schedule the pipeline
        p.auto_schedule(target);
    }

    non_local_means.print_loop_nest();

    // Benchmark the schedule
    Image<float> out(input.width(), input.height(), input.channels());
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t*1000;
}

int main(int argc, char **argv) {

    double manual_time = run_test(false);
    double auto_time = run_test(true);

    std::cout << "======================" << std::endl;
    std::cout << "Manual time: " << manual_time << "ms" << std::endl;
    std::cout << "Auto time: " << auto_time << "ms" << std::endl;
    std::cout << "======================" << std::endl;
    printf("Success!\n");
    return 0;
}
