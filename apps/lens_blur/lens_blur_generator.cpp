#include "Halide.h"

#include <algorithm>

namespace {

using namespace Halide;

class LensBlur : public Halide::Generator<LensBlur> {
public:
    Input<Buffer<uint8_t, 3>> left_im{"left_im"};
    Input<Buffer<uint8_t, 3>> right_im{"right_im"};
    // The number of displacements to consider
    Input<int> slices{"slices", 32, 1, 64};
    // The depth to focus on
    Input<int> focus_depth{"focus_depth", 13, 1, 32};
    // The increase in blur radius with misfocus depth
    Input<float> blur_radius_scale{"blur_radius_scale", 0.5f, 0.0f, 1.0f};
    // The number of samples of the aperture to use
    Input<int> aperture_samples{"aperture_samples", 32, 1, 64};

    Output<Buffer<float, 3>> final{"final"};

    void generate() {
        /* THE ALGORITHM */
        Expr maximum_blur_radius =
            cast<int>(max(slices - focus_depth, focus_depth) * blur_radius_scale);

        Func left = BoundaryConditions::repeat_edge(left_im);
        Func right = BoundaryConditions::repeat_edge(right_im);

        Func diff;
        diff(x, y, z, c) = min(absd(left(x, y, c), right(x + 2 * z, y, c)),
                               absd(left(x, y, c), right(x + 2 * z + 1, y, c)));

        Func cost;
        cost(x, y, z) = (pow(cast<float>(diff(x, y, z, 0)), 2) +
                         pow(cast<float>(diff(x, y, z, 1)), 2) +
                         pow(cast<float>(diff(x, y, z, 2)), 2));

        // Compute confidence of cost estimate at each pixel by taking the
        // variance across the stack.
        Func cost_confidence;
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
            mux(c, {cost(x, y, z) * cost_confidence(x, y), cost_confidence(x, y)});

        Expr w = left_im.dim(0).extent(), h = left_im.dim(1).extent();
        for (int i = 1; i < 8; i++) {
            cost_pyramid_push[i](x, y, z, c) = downsample(cost_pyramid_push[i - 1])(x, y, z, c);
            w /= 2;
            h /= 2;
            cost_pyramid_push[i] = BoundaryConditions::repeat_edge(cost_pyramid_push[i], {{0, w}, {0, h}});
        }

        Func cost_pyramid_pull[8];
        cost_pyramid_pull[7](x, y, z, c) = cost_pyramid_push[7](x, y, z, c);
        for (int i = 6; i >= 0; i--) {
            cost_pyramid_pull[i](x, y, z, c) = lerp(upsample(cost_pyramid_pull[i + 1])(x, y, z, c),
                                                    cost_pyramid_push[i](x, y, z, c),
                                                    0.5f);
        }

        Func filtered_cost;
        filtered_cost(x, y, z) = (cost_pyramid_pull[0](x, y, z, 0) /
                                  cost_pyramid_pull[0](x, y, z, 1));

        // Assume the minimum cost slice is the correct depth.
        Func depth;
        {
            RDom r(0, slices);
            depth(x, y) = argmin(filtered_cost(x, y, r))[0];
        }

        Func bokeh_radius;
        bokeh_radius(x, y) = abs(depth(x, y) - focus_depth) * blur_radius_scale;

        Func bokeh_radius_squared;
        bokeh_radius_squared(x, y) = pow(bokeh_radius(x, y), 2);

        // Take a max filter of the bokeh radius to determine the
        // worst-case bokeh radius to consider at each pixel. Makes the
        // sampling more efficient below.
        Func worst_case_bokeh_radius_y;
        Func worst_case_bokeh_radius;
        {
            RDom r(-maximum_blur_radius, 2 * maximum_blur_radius + 1);
            worst_case_bokeh_radius_y(x, y) = maximum(bokeh_radius(x, y + r));
            worst_case_bokeh_radius(x, y) = maximum(worst_case_bokeh_radius_y(x + r, y));
        }

        Func input_with_alpha;
        input_with_alpha(x, y, c) = mux(c, {cast<float>(left(x, y, 0)),
                                            cast<float>(left(x, y, 1)),
                                            cast<float>(left(x, y, 2)),
                                            255.0f});

        // Render a blurred image
        Func output;
        output(x, y, c) = input_with_alpha(x, y, c);

        // The sample locations are a random function of x, y, and sample
        // number (not c).
        Expr worst_radius = worst_case_bokeh_radius(x, y);
        Expr sample_u = (random_float() - 0.5f) * 2 * worst_radius;
        Expr sample_v = (random_float() - 0.5f) * 2 * worst_radius;
        sample_u = clamp(cast<int>(sample_u), -maximum_blur_radius, maximum_blur_radius);
        sample_v = clamp(cast<int>(sample_v), -maximum_blur_radius, maximum_blur_radius);
        Func sample_locations;
        sample_locations(x, y, z) = {sample_u, sample_v};

        RDom s(0, aperture_samples);
        sample_u = sample_locations(x, y, z)[0];
        sample_v = sample_locations(x, y, z)[1];
        Expr sample_x = x + sample_u, sample_y = y + sample_v;
        Expr r_squared = sample_u * sample_u + sample_v * sample_v;

        // We use this sample if it's from a pixel whose bokeh influences
        // this output pixel. Here's a crude approximation that ignores
        // some subtleties of occlusion edges and inpaints behind objects.
        Expr sample_is_within_bokeh_of_this_pixel =
            r_squared < bokeh_radius_squared(x, y);

        Expr this_pixel_is_within_bokeh_of_sample =
            r_squared < bokeh_radius_squared(sample_x, sample_y);

        Expr sample_is_in_front_of_this_pixel =
            depth(sample_x, sample_y) < depth(x, y);

        Func sample_weight;
        sample_weight(x, y, z) =
            select((sample_is_within_bokeh_of_this_pixel ||
                    sample_is_in_front_of_this_pixel) &&
                       this_pixel_is_within_bokeh_of_sample,
                   1.0f, 0.0f);

        sample_x = x + sample_locations(x, y, s)[0];
        sample_y = y + sample_locations(x, y, s)[1];
        output(x, y, c) += sample_weight(x, y, s) * input_with_alpha(sample_x, sample_y, c);

        // Normalize
        final(x, y, c) = output(x, y, c) / output(x, y, 3);

        /* ESTIMATES */
        // (This can be useful in conjunction with RunGen and benchmarks as well
        // as auto-schedule, so we do it in all cases.)
        // Provide estimates on the input image
        left_im.set_estimates({{0, 192}, {0, 320}, {0, 3}});
        right_im.set_estimates({{0, 192}, {0, 320}, {0, 3}});
        // Provide estimates on the parameters
        slices.set_estimate(32);
        focus_depth.set_estimate(13);
        blur_radius_scale.set_estimate(0.5f);
        aperture_samples.set_estimate(32);
        // Provide estimates on the pipeline output
        final.set_estimates({{0, 192}, {0, 320}, {0, 3}});

        /* THE SCHEDULE */
        if (auto_schedule) {
            // nothing
        } else if (get_target().has_gpu_feature()) {
            // Manual GPU schedule
            Var xi("xi"), yi("yi"), zi("zi");
            cost_pyramid_push[0]
                .compute_root()
                .reorder(c, z, x, y)
                .bound(c, 0, 2)
                .unroll(c)
                .gpu_tile(x, y, xi, yi, 16, 16);
            cost.compute_at(cost_pyramid_push[0], xi);
            cost_confidence.compute_at(cost_pyramid_push[0], xi);

            for (int i = 1; i < 8; i++) {
                cost_pyramid_push[i].compute_root().gpu_tile(x, y, z, xi, yi, zi, 8, 8, 8);
                cost_pyramid_pull[i].compute_root().gpu_tile(x, y, z, xi, yi, zi, 8, 8, 8);
            }

            depth.compute_root()
                .gpu_tile(x, y, xi, yi, 16, 16);
            input_with_alpha.compute_root()
                .reorder(c, x, y)
                .unroll(c)
                .gpu_tile(x, y, xi, yi, 16, 16);
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
            // Manual CPU schedule
            cost_pyramid_push[0]
                .compute_root()
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
                cost_pyramid_push[i].compute_at(cost_pyramid_pull[1], t).vectorize(x, 8);
                if (i > 1) {
                    cost_pyramid_pull[i]
                        .compute_at(cost_pyramid_pull[1], t)
                        .tile(x, y, xi, yi, 8, 2)
                        .vectorize(xi)
                        .unroll(yi);
                }
            }

            cost_pyramid_pull[1]
                .compute_root()
                .fuse(z, c, t)
                .parallel(t)
                .tile(x, y, xi, yi, 8, 2)
                .vectorize(xi)
                .unroll(yi);
            depth.compute_root()
                .tile(x, y, xi, yi, 8, 2)
                .vectorize(xi)
                .unroll(yi)
                .parallel(y, 8);
            input_with_alpha.compute_root()
                .reorder(c, x, y)
                .unroll(c)
                .vectorize(x, 8)
                .parallel(y, 8);
            worst_case_bokeh_radius_y
                .compute_at(final, y)
                .vectorize(x, 8);
            final.compute_root()
                .reorder(c, x, y)
                .bound(c, 0, 3)
                .unroll(c)
                .vectorize(x, 8)
                .parallel(y);
            worst_case_bokeh_radius
                .compute_at(final, y)
                .vectorize(x, 8);
            output.compute_at(final, x)
                .vectorize(x);
            output.update()
                .reorder(c, x, s)
                .vectorize(x)
                .unroll(c);
            sample_weight.compute_at(output, x).unroll(x);
            sample_locations.compute_at(output, x).vectorize(x);
        }
    }

private:
    Var x, y, z, c;

    // Downsample with a 1 3 3 1 filter
    Func downsample(Func f) {
        using Halide::_;
        Func downx, downy;
        downx(x, y, _) = (f(2 * x - 1, y, _) + 3.0f * (f(2 * x, y, _) + f(2 * x + 1, y, _)) + f(2 * x + 2, y, _)) / 8.0f;
        downy(x, y, _) = (downx(x, 2 * y - 1, _) + 3.0f * (downx(x, 2 * y, _) + downx(x, 2 * y + 1, _)) + downx(x, 2 * y + 2, _)) / 8.0f;
        return downy;
    }

    // Upsample using bilinear interpolation
    Func upsample(Func f) {
        using Halide::_;
        Func upx, upy;
        upx(x, y, _) = 0.25f * f((x / 2) - 1 + 2 * (x % 2), y, _) + 0.75f * f(x / 2, y, _);
        upy(x, y, _) = 0.25f * upx(x, (y / 2) - 1 + 2 * (y % 2), _) + 0.75f * upx(x, y / 2, _);
        return upy;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(LensBlur, lens_blur)
