#include "Halide.h"

namespace {

class StencilChain : public Halide::Generator<StencilChain> {
public:
    GeneratorParam<bool>    auto_schedule{"auto_schedule", false};
    GeneratorParam<int>     stencils{"stencils", 8, 1, 100};

    Input<Buffer<uint16_t>> input{"input", 2};
    Output<Buffer<uint16_t>> output{"output", 2};

    void generate() {

        std::vector<Func> stages;

        Var x, y;

        Func f("input_wrap");
        f(x, y) = input(x, y);

        stages.push_back(f);

        for (int s = 0; s < (int)stencils; s++) {
            Func f("stage_" + std::to_string(s));
            Expr e = cast<uint16_t>(0);
            for (int i = -2; i <= 2; i++) {
                for (int j = -2; j <= 2; j++) {
                    e += stages.back()(x+i, y+j);
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
            // Auto schedule the pipeline: this calls auto_schedule() for
            // all of the Outputs in this Generator
            auto_schedule_outputs();
        } else {
            // cpu schedule. No fusion.
            for (auto s : stages) {
                s.compute_root().parallel(s.args()[1]).vectorize(s.args()[0], 16);
            }
            output.compute_root().parallel(y).vectorize(x, 16);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(StencilChain, stencil_chain)
