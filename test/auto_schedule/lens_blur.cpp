#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

#include <iostream>
#include <limits>

Var x("x"), y("y"), z("z"), c("c");

// Downsample with a 1 3 3 1 filter
Func downsample(Func f) {
    Func downx("downx"), downy("downy");
    downx(x, y, _) = (f(2*x-1, y, _) + 3.0f * (f(2*x, y, _) + f(2*x+1, y, _)) + f(2*x+2, y, _)) / 8.0f;
    downy(x, y, _) = (downx(x, 2*y-1, _) + 3.0f * (downx(x, 2*y, _) + downx(x, 2*y+1, _)) + downx(x, 2*y+2, _)) / 8.0f;
    return downy;
}

// Upsample using bilinear interpolation
Func upsample(Func f) {
    Func upx("upx"), upy("upy");
    upx(x, y, _) = 0.25f * f((x/2) - 1 + 2*(x % 2), y, _) + 0.75f * f(x/2, y, _);
    upy(x, y, _) = 0.25f * upx(x, (y/2) - 1 + 2*(y % 2), _) + 0.75f * upx(x, y/2, _);
    return upy;
}

double run_test(bool auto_schedule) {
    int H = 1536;
    int W = 2560;
    Buffer<uint8_t> left_im(H, W, 3);
    Buffer<uint8_t> right_im(H, W, 3);

    for (int y = 0; y < left_im.height(); y++) {
        for (int x = 0; x < left_im.width(); x++) {
            for (int c = 0; c < 3; c++) {
                left_im(x, y, c) = rand() & 0xfff;
                right_im(x, y, c) = rand() & 0xfff;
            }
        }
    }

    //ImageParam left_im(UInt(8), 3, "left_im");
    //ImageParam right_im(UInt(8), 3, "right_im");

    // The number of displacements to consider
    //const int slices = 32;
    Param<int> slices;
    slices.set_range(1, 256);

    // The depth to focus on
    const int focus_depth = 13;

    // The increase in blur radius with misfocus depth
    const float blur_radius_scale = 0.5;

    // The number of samples of the aperture to use
    const int aperture_samples = 32;

    Expr maximum_blur_radius =
        cast<int>(max(slices - focus_depth, focus_depth) * blur_radius_scale);

    Func left = BoundaryConditions::repeat_edge(left_im);
    Func right = BoundaryConditions::repeat_edge(right_im);

    Func diff("diff");
    diff(x, y, z, c) = min(absd(left(x, y, c), right(x + 2*z, y, c)),
                           absd(left(x, y, c), right(x + 2*z + 1, y, c)));

    Func cost("cost");
    cost(x, y, z) = (pow(cast<float>(diff(x, y, z, 0)), 2) +
                     pow(cast<float>(diff(x, y, z, 1)), 2) +
                     pow(cast<float>(diff(x, y, z, 2)), 2));

    // Compute confidence of cost estimate at each pixel by taking the
    // variance across the stack.
    Func cost_confidence("cost_confidence");
    {
        RDom r(0, slices);
        Expr a = sum(pow(cost(x, y, r), 2)) / slices;
        Expr b = pow(sum(cost(x, y, r) / slices), 2);
        cost_confidence(x, y) = a - b;
    }

    // Do a push-pull thing to blur the cost volume with an
    // exponential-decay type thing to inpaint over regions with low
    // confidence.
    Func cost_pyramid_push[8];
    cost_pyramid_push[0](x, y, z, c) =
        select(c == 0, cost(x, y, z) * cost_confidence(x, y), cost_confidence(x, y));

    Expr w = left_im.width(), h = left_im.height();
    for (int i = 1; i < 8; i++) {
        cost_pyramid_push[i](x, y, z, c) = downsample(cost_pyramid_push[i-1])(x, y, z, c);
        w /= 2;
        h /= 2;
        cost_pyramid_push[i] = BoundaryConditions::repeat_edge(cost_pyramid_push[i], {{0, w}, {0, h}});
    }

    Func cost_pyramid_pull[8];
    cost_pyramid_pull[7](x, y, z, c) = cost_pyramid_push[7](x, y, z, c);
    for (int i = 6; i >= 0; i--) {
        cost_pyramid_pull[i](x, y, z, c) = lerp(upsample(cost_pyramid_pull[i+1])(x, y, z, c),
                                                cost_pyramid_push[i](x, y, z, c),
                                                0.5f);
    }

    Func filtered_cost("filtered_cost");
    filtered_cost(x, y, z) = (cost_pyramid_pull[0](x, y, z, 0) /
                              cost_pyramid_pull[0](x, y, z, 1));

    // Assume the minimum cost slice is the correct depth.
    Func depth("depth");
    {
        RDom r(0, slices);
        depth(x, y) = argmin(filtered_cost(x, y, r))[0];
    }

    Func bokeh_radius("bokeh_radius");
    bokeh_radius(x, y) = abs(depth(x, y) - focus_depth) * blur_radius_scale;

    Func bokeh_radius_squared("bokeh_radius_squared");
    bokeh_radius_squared(x, y) = pow(bokeh_radius(x, y), 2);

    // Take a max filter of the bokeh radius to determine the
    // worst-case bokeh radius to consider at each pixel. Makes the
    // sampling more efficient below.
    Func worst_case_bokeh_radius_y("worst_case_bokeh_radius_y");
    Func worst_case_bokeh_radius("worst_case_bokeh_radius");
    {
        RDom r(-maximum_blur_radius, 2*maximum_blur_radius+1);
        worst_case_bokeh_radius_y(x, y) = maximum(bokeh_radius(x, y + r));
        worst_case_bokeh_radius(x, y) = maximum(worst_case_bokeh_radius_y(x + r, y));
    }

    Func input_with_alpha("input_with_alpha");
    input_with_alpha(x, y, c) = select(c == 0, cast<float>(left(x, y, 0)),
                                       c == 1, cast<float>(left(x, y, 1)),
                                       c == 2, cast<float>(left(x, y, 2)),
                                       255.0f);

    // Render a blurred image
    Func output("output");
    output(x, y, c) = input_with_alpha(x, y, c);

    // The sample locations are a random function of x, y, and sample
    // number (not c).
    Expr worst_radius = worst_case_bokeh_radius(x, y);
    Expr sample_u = (random_float() - 0.5f) * 2 * worst_radius;
    Expr sample_v = (random_float() - 0.5f) * 2 * worst_radius;
    sample_u = clamp(cast<int>(sample_u), -maximum_blur_radius, maximum_blur_radius);
    sample_v = clamp(cast<int>(sample_v), -maximum_blur_radius, maximum_blur_radius);
    Func sample_locations("sample_locations");
    sample_locations(x, y, z) = {sample_u, sample_v};

    RDom s(0, aperture_samples);
    sample_u = sample_locations(x, y, z)[0];
    sample_v = sample_locations(x, y, z)[1];
    Expr sample_x = x + sample_u, sample_y = y + sample_v;
    Expr r_squared = sample_u * sample_u + sample_v * sample_v;

    // We use this sample if it's from a pixel whose bokeh influences
    // this output pixel. Here's a crude approximation that ignores
    // some subtleties of occlusion edges and inpaints behind objects.
    Expr sample_is_within_bokeh_of_this_pixel = r_squared < bokeh_radius_squared(x, y);
    Expr this_pixel_is_within_bokeh_of_sample = r_squared < bokeh_radius_squared(sample_x, sample_y);
    Expr sample_is_in_front_of_this_pixel = depth(sample_x, sample_y) < depth(x, y);

    Func sample_weight("sample_weight");
    sample_weight(x, y, z) =
        select((sample_is_within_bokeh_of_this_pixel ||
                sample_is_in_front_of_this_pixel) &&
               this_pixel_is_within_bokeh_of_sample,
               1.0f, 0.0f);

    sample_x = x + sample_locations(x, y, s)[0];
    sample_y = y + sample_locations(x, y, s)[1];
    output(x, y, c) += sample_weight(x, y, s) * input_with_alpha(sample_x, sample_y, c);

    // Normalize
    Func final("final");
    final(x, y, c) = output(x, y, c) / output(x, y, 3);

    // Auto-schedule the pipeline
    Target target = get_target_from_environment();
    Pipeline p(final);

    if (!auto_schedule) {
        if (target.has_gpu_feature()) {
            Var xi, yi, zi;
            cost_pyramid_push[0].compute_root()
                .reorder(c, z, x, y)
                .bound(c, 0, 2)
                .unroll(c)
                .gpu_tile(x, y, xi, yi, 16, 16);

            cost.compute_at(cost_pyramid_push[0], xi);
            cost_confidence.compute_at(cost_pyramid_push[0], xi);

            for (int i = 1; i < 8; i++) {
                cost_pyramid_push[i].compute_root()
                    .gpu_tile(x, y, z, xi, yi, zi, 8, 8, 8);
                cost_pyramid_pull[i].compute_root()
                    .gpu_tile(x, y, z, xi, yi, zi, 8, 8, 8);
            }

            depth.compute_root()
                .gpu_tile(x, y, xi, yi, 16, 16);

            input_with_alpha.compute_root()
                .reorder(c, x, y).unroll(c).gpu_tile(x, y, xi, yi, 16, 16);

            worst_case_bokeh_radius_y
                .compute_root()
                .gpu_tile(x, y, xi, yi, 16, 16);

            worst_case_bokeh_radius
                .compute_root()
                .gpu_tile(x, y, xi, yi, 16, 16);

            final.compute_root()
                .reorder(c, x, y)
                .bound(c, 0, 3)
                .unroll(c)
                .gpu_tile(x, y, xi, yi, 16, 16);

            output.compute_at(final, xi);
            output.update().reorder(c, x, s).unroll(c);
            sample_weight.compute_at(output, x);
            sample_locations.compute_at(output, x);

        } else {
            // Andrew
            // bokeh_radius is a pretty simple function of depth. Maybe I should inline it.

            cost_pyramid_push[0].compute_root()
                .reorder(c, z, x, y)
                .bound(c, 0, 2)
                .unroll(c)
                .vectorize(x, 16)
                .parallel(y, 4);
            cost.compute_at(cost_pyramid_push[0], x)
                .vectorize(x);
            cost_confidence.compute_at(cost_pyramid_push[0], x)
                .vectorize(x);

            Var xi, yi, t;
            for (int i = 1; i < 8; i++) {
                cost_pyramid_push[i].compute_at(cost_pyramid_pull[1], t)
                    .vectorize(x, 8);
                if (i > 1) {
                    cost_pyramid_pull[i].compute_at(cost_pyramid_pull[1], t)
                        .tile(x, y, xi, yi, 8, 2)
                        .vectorize(xi)
                        .unroll(yi);
                }
            }


            cost_pyramid_pull[1].compute_root()
                .fuse(z, c, t).parallel(t)
                .tile(x, y, xi, yi, 8, 2).vectorize(xi).unroll(yi);

            depth.compute_root()
                .tile(x, y, xi, yi, 8, 2).vectorize(xi).unroll(yi)
                .parallel(y, 8);

            input_with_alpha.compute_root().reorder(c, x, y).unroll(c).vectorize(x, 8).parallel(y, 8);

            worst_case_bokeh_radius_y.compute_at(final, y).vectorize(x, 8);

            final.compute_root().reorder(c, x, y).bound(c, 0, 3).unroll(c).vectorize(x, 8).parallel(y);
            worst_case_bokeh_radius.compute_at(final, y).vectorize(x, 8);
            output.compute_at(final, x).vectorize(x);
            output.update().reorder(c, x, s).vectorize(x).unroll(c);
            sample_weight.compute_at(output, x).unroll(x);
            sample_locations.compute_at(output, x).vectorize(x);

            // Ran at 4:01: 111ms

            // At this point, I think I've converged.
        }
    } else {
        /*left_im.dim(0).set_bounds_estimate(0, left_img.width());
        left_im.dim(1).set_bounds_estimate(0, left_img.height());
        left_im.dim(2).set_bounds_estimate(0, 3);

        right_im.dim(0).set_bounds_estimate(0, right_img.width());
        right_im.dim(1).set_bounds_estimate(0, right_img.height());
        right_im.dim(2).set_bounds_estimate(0, 3);*/

        final.estimate(x, 0, left_im.width())
            .estimate(y, 0, left_im.height())
            .estimate(c, 0, 3);
        p.auto_schedule(target);
    }

    if (auto_schedule) {
        p.compile_to_lowered_stmt("lens_blur.html", {left_im, right_im, slices}, HTML, target);
    } else {
        p.compile_to_lowered_stmt("lens_blur_manual.html", {left_im, right_im, slices}, HTML, target);
    }
    // Inspect the schedule
    //final.print_loop_nest();

    //left_im.set(left_img);
    //right_im.set(right_img);

    slices.set(32);

    // Run the schedule
    Buffer<float> out(left_im.width(), left_im.height(), 3);
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

    if (!get_target_from_environment().has_gpu_feature() &&
        (auto_time > manual_time * 2)) {
        printf("Auto-scheduler is much much slower than it should be.\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
