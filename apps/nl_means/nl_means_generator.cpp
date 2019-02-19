#include "Halide.h"
#include "../autoscheduler/SimpleAutoSchedule.h"

namespace {

using namespace Halide;

class NonLocalMeans : public Halide::Generator<NonLocalMeans> {
public:
    Input<Buffer<float>>  input{"input", 3};
    Input<int>            patch_size{"patch_size"};
    Input<int>            search_area{"search_area"};
    Input<float>          sigma{"sigma"};

    Output<Buffer<float>> non_local_means{"non_local_means", 3};

    void generate() {
        /* THE ALGORITHM */

        // This implements the basic description of non-local means found at
        // https://en.wikipedia.org/wiki/Non-local_means

        Var x("x"), y("y"), c("c");

        Expr inv_sigma_sq = -1.0f/(sigma*sigma*patch_size*patch_size);

        // Add a boundary condition
        Func clamped = BoundaryConditions::repeat_edge(input);

        // Define the difference images
        Var dx("dx"), dy("dy");
        Func dc("d");
        dc(x, y, dx, dy, c) = pow(clamped(x, y, c) - clamped(x + dx, y + dy, c), 2);

        // Sum across color channels
        RDom channels(0, 3);
        Func d("d");
        d(x, y, dx, dy) = sum(dc(x, y, dx, dy, channels));

        // Find the patch differences by blurring the difference images
        RDom patch_dom(-(patch_size/2), patch_size);
        Func blur_d_y("blur_d_y");
        blur_d_y(x, y, dx, dy) = sum(d(x, y + patch_dom, dx, dy));

        Func blur_d("blur_d");
        blur_d(x, y, dx, dy) = sum(blur_d_y(x + patch_dom, y, dx, dy));

        // Compute the weights from the patch differences
        Func w("w");
        w(x, y, dx, dy) = fast_exp(blur_d(x, y, dx, dy)*inv_sigma_sq);

        // Add an alpha channel
        Func clamped_with_alpha("clamped_with_alpha");
        clamped_with_alpha(x, y, c) = select(c == 0, clamped(x, y, 0),
                                             c == 1, clamped(x, y, 1),
                                             c == 2, clamped(x, y, 2),
                                             1.0f);

        // Define a reduction domain for the search area
        RDom s_dom(-(search_area/2), search_area, -(search_area/2), search_area);

        // Compute the sum of the pixels in the search area
        Func non_local_means_sum("non_local_means_sum");
        non_local_means_sum(x, y, c) += w(x, y, s_dom.x, s_dom.y) * clamped_with_alpha(x + s_dom.x, y + s_dom.y, c);

        non_local_means(x, y, c) =
            clamp(non_local_means_sum(x, y, c) / non_local_means_sum(x, y, 3), 0.0f, 1.0f);

        /* THE SCHEDULE */

        // Require 3 channels for output
        non_local_means.dim(2).set_bounds(0, 3);

        Var tx("tx"), ty("ty"), xi("xi"), yi("yi");

        if (auto_schedule) {
            // Provide estimates on the input image
            input.dim(0).set_bounds_estimate(0, 1536);
            input.dim(1).set_bounds_estimate(0, 2560);
            input.dim(2).set_bounds_estimate(0, 3);
            // Provide estimates on the parameters
            patch_size.set_estimate(7);
            search_area.set_estimate(7);
            sigma.set_estimate(0.12f);
            // Provide estimates on the output pipeline
            non_local_means.estimate(x, 0, 1536)
                .estimate(y, 0, 2560)
                .estimate(c, 0, 3);
        } else if (get_target().has_gpu_feature()) {
            non_local_means.compute_root()
                .gpu_tile(x, y, xi, yi, 32, 8);
            non_local_means_sum.compute_root()
                .reorder(c, x, y).unroll(c)
                .gpu_tile(x, y, xi, yi, 32, 8)
                .update()
                .reorder(c, s_dom.x, x, y, s_dom.y)
                .unroll(c)
                .gpu_tile(x, y, xi, yi, 32, 8);
            w.compute_at(non_local_means_sum, s_dom.y).store_root()
                .reorder(dx, dy, x, y)
                .gpu_tile(x, y, xi, yi, 32, 8);
            blur_d_y.compute_at(non_local_means_sum, s_dom.y).store_root()
                .reorder(dx, dy, x, y)
                .gpu_tile(x, y, xi, yi, 32, 8);
            d.compute_at(non_local_means_sum, s_dom.y).store_root()
                .reorder(dx, dy, x, y)
                .gpu_tile(x, y, xi, yi, 32, 8);
        } else {
            std::string use_simple_autoscheduler =
                Halide::Internal::get_env_variable("HL_USE_SIMPLE_AUTOSCHEDULER");
            if (use_simple_autoscheduler == "1") {
                Halide::SimpleAutoscheduleOptions options;
                options.gpu = get_target().has_gpu_feature();
                options.gpu_tile_channel = 3;
                Func output_func = non_local_means;
                Halide::simple_autoschedule(output_func,
                                    {{"input.min.0", 0},
                                     {"input.extent.0", 1536},
                                     {"input.min.1", 0},
                                     {"input.extent.1", 2560},
                                     {"input.min.2", 0},
                                     {"input.extent.2", 3},
                                     {"patch_size", 7},
                                     {"search_area", 7},
                                     {"sigma", 0.12f}},
                                    {{0, 1536},
                                     {0, 2560},
                                     {0, 3}},
                                    options);
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
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(NonLocalMeans, nl_means)
