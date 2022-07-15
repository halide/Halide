#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

double run_test(bool auto_schedule) {
    Func fib("fib"), g("g");
    Var x("x");
    RDom r(2, 298, "r");

    fib(x) = 1;
    fib(r) = fib(r - 2) + fib(r - 1);

    g(x) = fib(x + 10);

    // Provide estimates on the pipeline output
    g.set_estimate(x, 0, 300);

    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    if (auto_schedule) {
        // Auto-schedule the pipeline
#ifdef HALIDE_ALLOW_LEGACY_AUTOSCHEDULER_API
        p.auto_schedule(target);
#else
        p.apply_autoscheduler(target, {"Mullapudi2016"});
#endif
    }

    // Inspect the schedule
    g.print_loop_nest();

    // Benchmark the schedule
    Buffer<int> out(100);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t * 1000;
}

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

    double manual_time = run_test(false);
    double auto_time = run_test(true);

    std::cout << "======================\n"
              << "Manual time: " << manual_time << "ms\n"
              << "Auto time: " << auto_time << "ms\n"
              << "======================\n";

    printf("Success!\n");
    return 0;
}
