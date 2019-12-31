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
        p.auto_schedule(target);
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
    double manual_time = run_test(false);
    double auto_time = run_test(true);

    std::cout << "======================" << std::endl;
    std::cout << "Manual time: " << manual_time << "ms" << std::endl;
    std::cout << "Auto time: " << auto_time << "ms" << std::endl;
    std::cout << "======================" << std::endl;

    printf("Success!\n");
    return 0;
}