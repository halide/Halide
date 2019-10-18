#include "Halide.h"
#include "../autoscheduler/SimpleAutoSchedule.h"

namespace {

class StencilChain : public Halide::Generator<StencilChain> {
public:
    GeneratorParam<int>     stencils{"stencils", 32, 1, 100};

    Input<Buffer<uint16_t>> input{"input", 2};
    Output<Buffer<uint16_t>> output{"output", 2};

    void generate() {

        std::vector<Func> stages;

        Var x("x"), y("y");

        Func f = Halide::BoundaryConditions::repeat_edge(input);

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

        /* ESTIMATES */
        // (This can be useful in conjunction with RunGen and benchmarks as well
        // as auto-schedule, so we do it in all cases.)
        {
            const int width = 1536;
            const int height = 2560;
            // Provide estimates on the input image
            input.set_estimates({{0, width}, {0, height}});
            // Provide estimates on the pipeline output
            output.set_estimates({{0, width}, {0, height}});
        }

        if (auto_schedule) {
            // nothing
        } else {
            std::string use_simple_autoscheduler =
                Halide::Internal::get_env_variable("HL_USE_SIMPLE_AUTOSCHEDULER");
            if (use_simple_autoscheduler == "1") {
                Halide::SimpleAutoscheduleOptions options;
                options.gpu = get_target().has_gpu_feature();
                options.gpu_tile_channel = 1;
                Func output_func = output;
                Halide::simple_autoschedule(output_func,
                                    {{"input.min.0", 0},
                                     {"input.extent.0", 1536},
                                     {"input.min.1", 0},
                                     {"input.extent.1", 2560}},
                                    {{0, 1536},
                                     {0, 2560}},
                                    options);
            } else if (get_target().has_gpu_feature()) {
                Var xi, yi;
                const int group_size = 4;
                int last_stage = (int)stages.size() - 1;
                stages[last_stage] = output;
                for (int i = 0; i <= last_stage; i++) {
                    Func s = stages[i];
                    x = s.args()[0];
                    y = s.args()[1];
                    int next_root = ((i + group_size - 1) / group_size) * group_size;
                    next_root = std::min(next_root, last_stage);
                    if (i == next_root) {
                        s.compute_root()
                            .gpu_tile(x, y, xi, yi, 64 - 4 * (group_size - 1), 8)
                            .unroll(xi, 2);
                    } else {
                        s.compute_at(stages[next_root], x).unroll(x, 2).gpu_threads(x, y);
                    }
                }
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
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(StencilChain, stencil_chain)
