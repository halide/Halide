#include "Halide.h"

namespace {

class StencilChain : public Halide::Generator<StencilChain> {
public:
    GeneratorParam<int>     stencils{"stencils", 8, 1, 100};

    Input<Buffer<uint16_t>> input{"input", 2};
    Output<Buffer<uint16_t>> output{"output", 2};

    void generate() {

        std::vector<Func> stages;

        Var x("x"), y("y");

        Func f("input_wrap");
        f(x, y) = input(x, y);

        stages.push_back(f);

        for (int s = 0; s < (int)stencils; s++) {
            Func f("stage_" + std::to_string(s));
            Expr e = cast<uint16_t>(0);
            for (int i = -2; i <= 2; i++) {
                for (int j = -2; j <= 2; j++) {
                    e += ((i+3)*(j+3))*stages.back()(x+i, y+j);
                }
            }
            f(x, y) = e;
            stages.push_back(f);
        }

        output(x, y) = stages.back()(x, y);

        if (auto_schedule) {
            // Provide estimates on the input image
            input.dim(0).set_bounds_estimate(0, 1536);
            input.dim(1).set_bounds_estimate(0, 2560);
            // Provide estimates on the pipeline output
            output.estimate(x, 32, 1536 - 64)
                .estimate(y, 32, 2560 - 64);
        } else {
            // CPU schedule. No fusion.
            Var yi, yo, xo, xi, t;
            for (size_t i = 1; i < stages.size() - 1; i++) {
                Func s = stages[i];
                s.store_at(output, t).compute_at(output, yi).vectorize(s.args()[0], 16);
            }
            output.compute_root()
                .tile(x, y, xo, yo, xi, yi, 512, 512)
                .fuse(xo, yo, t)
                .parallel(t)
                .vectorize(xi, 16);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(StencilChain, stencil_chain)
