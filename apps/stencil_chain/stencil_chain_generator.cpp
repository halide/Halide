#include "Halide.h"

namespace {

class StencilChain : public Halide::Generator<StencilChain> {
public:
    GeneratorParam<int> stencils{"stencils", 32, 1, 100};

    Input<Buffer<uint16_t, 2>> input{"input"};
    Output<Buffer<uint16_t, 2>> output{"output"};

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

            // 2.9 ms on a 2060 RTX

            // It seems that just compute-rooting all the stencils is
            // fastest on this GPU, plus some unrolling and aggressive
            // staging to share loads between adjacent pixels.
            Var xi, yi, xii, yii;
            stages.pop_back();  // Inline the second-last stage into the output
            stages.push_back(output);
            for (size_t i = 1; i < stages.size(); i++) {
                Func &s = stages[i];
                Func prev = stages[i - 1];
                x = s.args()[0];
                y = s.args()[1];
                s.compute_root()
                    .gpu_tile(x, y, xi, yi, 30 * 2, 12)
                    .tile(xi, yi, xii, yii, 2, 2)
                    .unroll(xii)
                    .unroll(yii);

                // Pre-load the entire region required of the previous
                // stage into shared memory by adding a wrapper Func
                // and scheduling it at blocks. This way instead of
                // every pixel doing 25 loads from global memory, many of
                // which overlap, we load each unique value from
                // global into shared once, and then we use faster
                // loads from shared in the actual stencil.
                prev.in()
                    .compute_at(s, x)
                    .tile(prev.args()[0], prev.args()[1], xi, yi, 2, 2)
                    .vectorize(xi)
                    .unroll(yi)
                    .gpu_threads(prev.args()[0], prev.args()[1]);

                // A similar benefit applies for the
                // vectorized/unrolled 2x2 tiles. Instead of having
                // each unrolled iteration do its own mix of scalar
                // and vector loads from shared memory in a 5x5
                // window, many of which get deduped across the block,
                // we load a 6x6 window of shared into registers using
                // only aligned vector loads, and then the actual
                // stencil pulls from those registers. We're adding
                // another wrapper Func around the wrapper Func we
                // created above, so we say .in().in()
                prev.in()
                    .in()
                    .compute_at(s, xi)
                    .vectorize(prev.args()[0], 2)
                    .unroll(prev.args()[0])
                    .unroll(prev.args()[1]);
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
