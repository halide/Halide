#include "Halide.h"

namespace {

class StencilChain : public Halide::Generator<StencilChain> {
public:
    GeneratorParam<int> stencils{"stencils", 32, 1, 100};

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
                    e += ((i + 3) * (j + 3)) * stages.back()(x + i, y + j);
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
        } else if (get_target().has_gpu_feature()) {
            // GPU schedule

            // 5.90 ms on a 2060 RTX

            // It seems that just compute-rooting all the stencils is
            // fastest on this GPU, plus some unrolling to share loads
            // between adjacent pixels.
            Var xi, yi, xii, yii;
            for (size_t i = 1; i < stages.size(); i++) {
                Func &s = stages[i];
                x = s.args()[0];
                y = s.args()[1];
                s.compute_root()
                    .gpu_tile(x, y, xi, yi, 64, 16)
                    .tile(xi, yi, xii, yii, 2, 2)
                    .unroll(xii)
                    .unroll(yii);
            }
        } else {
            // CPU schedule
            // 4.23ms on an Intel i9-9960X using 16 threads at 3.5
            // GHz.

            // Runtime is pretty noisy, so benchmarked over 1000
            // trials instead of the default of 10 in the
            // Makefile. This uses AVX-512 instructions, but not
            // floating-point ones. My CPU seems to hover at 3.5GHz on
            // this workload.

            const int vec = natural_vector_size<uint16_t>();

            // How many stencils in between each compute-root
            const int group_size = 11;
            Var yi, yo, xo, xi, t;

            const int last_stage_idx = (int)stages.size() - 1;
            for (int j = last_stage_idx; j > 0; j -= group_size) {
                Func out = (j == last_stage_idx) ? output : stages[j];

                const int stages_to_output = last_stage_idx - j;
                const int expansion = 4 * stages_to_output;
                const int w = 1536 + expansion;
                const int h = 2560 + expansion;

                out.compute_root()
                    // Break into 16 tiles for our 16 threads
                    .tile(x, y, xo, yo, xi, yi, w / 4, h / 4)
                    .fuse(xo, yo, t)
                    .parallel(t)
                    .vectorize(xi, vec);

                for (int i = std::max(0, j - group_size + 1); i < j; i++) {
                    Func s = stages[i];
                    s.store_at(out, t)
                        .compute_at(out, yi)
                        .vectorize(s.args()[0], vec);
                }
            }
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(StencilChain, stencil_chain)
