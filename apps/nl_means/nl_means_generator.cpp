#include "Halide.h"

namespace {

using namespace Halide;

class NonLocalMeans : public Halide::Generator<NonLocalMeans> {
public:
    Input<Buffer<float, 3>> input{"input"};
    Input<int> patch_size{"patch_size"};
    Input<int> search_area{"search_area"};
    Input<float> sigma{"sigma"};

    Output<Buffer<float, 3>> non_local_means{"non_local_means"};

    void generate() {
        /* THE ALGORITHM */

        // This implements the basic description of non-local means found at
        // https://en.wikipedia.org/wiki/Non-local_means

        Var x("x"), y("y"), c("c");

        Expr inv_sigma_sq = -1.0f / (sigma * sigma * patch_size * patch_size);

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
        RDom patch_dom(-(patch_size / 2), patch_size);
        Func blur_d_y("blur_d_y");
        blur_d_y(x, y, dx, dy) = sum(d(x, y + patch_dom, dx, dy));

        Func blur_d("blur_d");
        blur_d(x, y, dx, dy) = sum(blur_d_y(x + patch_dom, y, dx, dy));

        // Compute the weights from the patch differences
        Func w("w");
        w(x, y, dx, dy) = fast_exp(blur_d(x, y, dx, dy) * inv_sigma_sq);

        // Add an alpha channel
        Func clamped_with_alpha("clamped_with_alpha");
        clamped_with_alpha(x, y, c) = mux(c, {clamped(x, y, 0), clamped(x, y, 1), clamped(x, y, 2), 1.0f});

        // Define a reduction domain for the search area
        RDom s_dom(-(search_area / 2), search_area, -(search_area / 2), search_area);

        // Compute the sum of the pixels in the search area
        Func non_local_means_sum("non_local_means_sum");
        non_local_means_sum(x, y, c) += w(x, y, s_dom.x, s_dom.y) * clamped_with_alpha(x + s_dom.x, y + s_dom.y, c);

        non_local_means(x, y, c) =
            clamp(non_local_means_sum(x, y, c) / non_local_means_sum(x, y, 3), 0.0f, 1.0f);

        /* THE SCHEDULE */

        // Require 3 channels for output
        non_local_means.dim(2).set_bounds(0, 3);

        Var tx("tx"), ty("ty"), xi("xi"), yi("yi");

        /* ESTIMATES */
        // (This can be useful in conjunction with RunGen and benchmarks as well
        // as auto-schedule, so we do it in all cases.)
        // Provide estimates on the input image
        input.set_estimates({{0, 1536}, {0, 2560}, {0, 3}});
        // Provide estimates on the parameters
        patch_size.set_estimate(7);
        search_area.set_estimate(7);
        sigma.set_estimate(0.12f);
        // Provide estimates on the output pipeline
        non_local_means.set_estimates({{0, 1536}, {0, 2560}, {0, 3}});

        if (using_autoscheduler()) {
            // nothing
        } else if (get_target().has_gpu_feature()) {
            // 22 ms on a 2060 RTX
            Var xii, yii;

            // We'll use 32x16 thread blocks throughout. This was
            // found by just trying lots of sizes, but large thread
            // blocks are particularly good in the blur_d stage to
            // avoid doing wasted blurring work at tile boundaries
            // (especially for large patch sizes).

            non_local_means.compute_root()
                .reorder(c, x, y)
                .unroll(c)
                .gpu_tile(x, y, xi, yi, 32, 16);

            non_local_means_sum.compute_root()
                .gpu_tile(x, y, xi, yi, 32, 16)
                .update()
                .reorder(c, s_dom.x, x, y, s_dom.y)
                .tile(x, y, xi, yi, 32, 16)
                .gpu_blocks(x, y)
                .gpu_threads(xi, yi)
                .unroll(c);

            // The patch size we're benchmarking for is 7, which
            // implies an expansion of 6 pixels for footprint of the
            // blur, so we'll size tiles of blur_d to be a multiple of
            // the thread block size minus 6.
            blur_d.compute_at(non_local_means_sum, s_dom.y)
                .tile(x, y, xi, yi, 128 - 6, 32 - 6)
                .tile(xi, yi, xii, yii, 32, 16)
                .gpu_threads(xii, yii)
                .gpu_blocks(x, y, dx);

            blur_d_y.compute_at(blur_d, x)
                .tile(x, y, xi, yi, 32, 16)
                .gpu_threads(xi, yi);

            d.compute_at(blur_d, x)
                .tile(x, y, xi, yi, 32, 16)
                .gpu_threads(xi, yi);

        } else {
            // 64 ms on an Intel i9-9960X using 32 threads at 3.0 GHz

            const int vec = natural_vector_size<float>();

            non_local_means.compute_root()
                .reorder(c, x, y)
                .tile(x, y, tx, ty, x, y, 16, 8)
                .parallel(ty)
                .vectorize(x, vec);
            blur_d_y.compute_at(non_local_means, tx)
                .hoist_storage(non_local_means, ty)
                .reorder(y, x)
                .vectorize(x, vec);
            d.compute_at(non_local_means, tx)
                .hoist_storage(non_local_means, ty)
                .vectorize(x, vec);
            non_local_means_sum.compute_at(non_local_means, x)
                .reorder(c, x, y)
                .bound(c, 0, 4)
                .unroll(c)
                .vectorize(x, vec);
            non_local_means_sum.update(0)
                .reorder(c, x, y, s_dom.x, s_dom.y)
                .unroll(c)
                .vectorize(x, vec);
            blur_d.compute_at(non_local_means_sum, x)
                .vectorize(x, vec);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(NonLocalMeans, nl_means)
