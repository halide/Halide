#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Var x;

    const int num_stages = 64;

    // Construct a DAG-structured pipeline where each leaf is an expensive
    // combination of several children, and some of the children are shared
    // between multiple parents. Built twice, with and without async(), so
    // both can be benchmarked in the same comparison.
    auto make_pipeline = [&](bool use_async) {
        std::vector<Func> stages(num_stages);
        for (int i = num_stages - 1; i >= 0; i--) {
            int child_1 = i * 2 + 1;
            int child_2 = i * 2 + 2;
            int child_3 = i * 2 + 3;
            // Initialize the stage.
            if (child_3 >= num_stages) {
                stages[i](x) = cast<float>(x + i);
            } else {
                stages[i](x) = stages[child_1](x) + stages[child_2](x) + stages[child_3](x);
            }
            // Now do something expensive and inherently serial
            RDom r(1, 1024 - 1, 0, 64);
            stages[i](r.x) = sin(stages[i](r.x - 1));

            stages[i].compute_root();
            if (use_async) {
                stages[i].async();
            }
        }

        stages[0].compile_jit();
        return stages[0];
    };

    Func stages_without_async = make_pipeline(false);
    Func stages_with_async = make_pipeline(true);

    Buffer<float> out_without_async(1024);
    Buffer<float> out_with_async(1024);

    auto [without_async, with_async] = benchmark_comparison(
        BenchmarkConfig{},
        [&]() { stages_without_async.realize(out_without_async); },
        [&]() { stages_with_async.realize(out_with_async); });

    double times[2] = {without_async.wall_time, with_async.wall_time};
    printf("Without async %f\n", times[0]);
    printf("With async %f\n", times[1]);

    if (times[0] < times[1]) {
        printf("Using async() was slower!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
