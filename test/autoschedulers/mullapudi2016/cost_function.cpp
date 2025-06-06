#include "Halide.h"
#include "get_autoscheduler_params.hpp"

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Autoschedulers do not support WebAssembly.\n");
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }

    load_plugin(argv[1]);

    int W = 6400;
    int H = 4800;
    Buffer<uint16_t> input(W, H);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = rand() & 0xfff;
        }
    }

    Var x("x"), y("y");

    int num_stencils = 15;

    std::vector<Func> stencils;
    for (int i = 0; i < num_stencils; i++) {
        Func s("stencil_" + std::to_string(i));
        stencils.push_back(s);
    }

    stencils[0](x, y) = (input(x, y) + input(x + 1, y) + input(x + 2, y)) / 3;
    for (int i = 1; i < num_stencils; i++) {
        stencils[i](x, y) = (stencils[i - 1](x, y) + stencils[i - 1](x, y + 1) +
                             stencils[i - 1](x, y + 2)) /
                            3;
    }

    // Provide estimates on the pipeline output
    stencils[num_stencils - 1].set_estimate(x, 0, 6200).set_estimate(y, 0, 4600);

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(stencils[num_stencils - 1]);

    // Reduce the estimated available shared memory (L2 cache) from 48kB to 25kB
    // to work around the following error:
    //
    // Error: CUDA error: CUDA_ERROR_INVALID_VALUE cuLaunchKernel failed
    //
    // This kernel launch failure occurs likely because:
    // (i) most Halide users run on consumer-grade GPUs (e.g., Nvidia GTX 1660)
    // with limited shared memory and/or register counts, and
    // (ii) the autoscheduler heuristics tend to underestimate the actual shared
    // memory consumed by GPU kernels.
    constexpr Mullapudi2016Params gpu_specifications{
        /* .last_level_cache_size = */ 25'000,
        /* .parallelism = */ 128,
    };

    AutoSchedulerResults results = p.apply_autoscheduler(
        target,
        get_autoscheduler_params(target.has_gpu_feature(),
                                 target.has_gpu_feature() ?
                                     std::optional<Mullapudi2016Params>{gpu_specifications} :
                                     std::nullopt));

    // Don't dump to stdout (this is only for debugging)
    // std::cout << "\n\n******************************************\nSCHEDULE:\n"
    //           << "******************************************\n"
    //           << results.schedule_source
    //           << "\n******************************************\n\n";

    // Inspect the schedule (only for debugging))
    // stencils[num_stencils - 1].print_loop_nest();

    // Run the schedule
    p.realize({6204, 4604});

    printf("Success!\n");
    return 0;
}
