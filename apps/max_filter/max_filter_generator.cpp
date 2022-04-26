#include "Halide.h"

namespace {

using namespace Halide::ConciseCasts;
using namespace Halide::BoundaryConditions;

class Max : public Halide::Generator<Max> {
public:
    GeneratorParam<int> radius_{"radius", 26};
    Input<Buffer<float, 3>> input_{"input"};
    Output<Buffer<float, 3>> output_{"output"};

    void generate() {
        Var x("x"), y("y"), c("c"), t("t");

        Func input = repeat_edge(input_,
                                 {{input_.dim(0).min(), input_.dim(0).extent()},
                                  {input_.dim(1).min(), input_.dim(1).extent()}});

        const int radius = radius_;
        const int slices = (int)(ceilf(logf(radius) / logf(2))) + 1;

        // A sequence of vertically-max-filtered versions of the input,
        // each filtered twice as tall as the previous slice. All filters
        // are downward-looking.
        Func vert_log("vert_log");
        vert_log(x, y, c, t) = input(x, y, c);
        RDom r(-radius, input_.height() + radius, 1, slices - 1);
        vert_log(x, r.x, c, r.y) = max(vert_log(x, r.x, c, r.y - 1),
                                       vert_log(x, r.x + clamp((1 << (r.y - 1)), 0, radius * 2), c, r.y - 1));

        // We're going to take a max filter of arbitrary diameter
        // by maxing two samples from its floor log 2 (e.g. maxing two
        // 8-high overlapping samples). This next Func tells us which
        // slice to draw from for a given radius:
        Func slice_for_radius("slice_for_radius");
        slice_for_radius(t) = cast<int>(floor(log(2 * t + 1) / logf(2)));

        // Produce every possible vertically-max-filtered version of the image:
        Func vert("vert");
        // t is the blur radius
        Expr slice = clamp(slice_for_radius(t), 0, slices);
        Expr first_sample = vert_log(x, y - t, c, slice);
        Expr second_sample = vert_log(x, y + t + 1 - clamp(1 << slice, 0, 2 * radius), c, slice);
        vert(x, y, c, t) = max(first_sample, second_sample);

        Func filter_height("filter_height");
        RDom dy(0, radius + 1);
        filter_height(x) = sum(select(x * x + dy * dy < (radius + 0.25f) * (radius + 0.25f), 1, 0));

        // Now take an appropriate horizontal max of them at each output pixel
        RDom dx(-radius, 2 * radius + 1);
        output_(x, y, c) = maximum(vert(x + dx, y, c, clamp(filter_height(dx), 0, radius + 1)));

        // Estimates (for autoscheduler; ignored otherwise)
        {
            input_.dim(0).set_estimate(0, 1536);
            input_.dim(1).set_estimate(0, 2560);
            input_.dim(2).set_estimate(0, 3);
            output_.dim(0).set_estimate(0, 1536);
            output_.dim(1).set_estimate(0, 2560);
            output_.dim(2).set_estimate(0, 3);
        }

        // Schedule
        if (!auto_schedule) {
            if (get_target().has_gpu_feature()) {
                // 11.8ms on a 2060 RTX

                slice_for_radius.compute_root();
                filter_height.compute_root();
                Var xi, xo, yi;

                output_.gpu_tile(x, y, xi, yi, 32, 8)
                    .gpu_blocks(x, y, c);

                // There's a boundary condition on the input, so let's
                // keep things simple with a RoundUp tail strategy.
                vert_log.compute_root()
                    .reorder(c, t, x, y)
                    .gpu_tile(x, y, xi, yi, 16, 16, TailStrategy::RoundUp)
                    .update()
                    .split(x, xo, xi, 32, TailStrategy::RoundUp)
                    .reorder(r.x, r.y, xi, xo, c)
                    .gpu_blocks(xo, c)
                    .gpu_threads(xi);

            } else {
                // 47ms on an Intel i9-9960X using 16 threads

                Var tx;
                // These don't matter, just LUTs
                slice_for_radius.compute_root();
                filter_height.compute_root();

                // vert_log.update() doesn't have enough parallelism, but I
                // can't figure out how to give it more... Split whole image
                // into slices.

                output_.compute_root()
                    .split(x, tx, x, 256)
                    .reorder(x, y, c, tx)
                    .fuse(c, tx, t)
                    .parallel(t)
                    .vectorize(x, 8);
                vert_log.compute_at(output_, t);
                vert_log.vectorize(x, 8);
                vert_log.update()
                    .reorder(x, r.x, r.y, c)
                    .vectorize(x, 8);
                vert.compute_at(output_, y)
                    .vectorize(x, 8);
            }
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Max, max_filter)
