#include "Halide.h"
#include <stdio.h>

using namespace Halide;

void run_test_1() {
    Param<int> offset;
    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");
    Func f("f");
    f(x, y) = input(x, y) * 2;

    Func g("g");
    g(x, y) = f(x + offset, y) + f(x - offset, y);

    // Provide estimates on the pipeline output
    g.set_estimates({{0, 1000}, {0, 1000}});

    // Provide estimates on the ImageParam
    input.set_estimates({{0, 1000}, {0, 1000}});

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // g.print_loop_nest();
}

void run_test_2() {
    Param<int> offset;
    offset.set_estimate(1);
    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");
    Func f("f");
    f(x, y) = input(x, y) * 2;

    Func g("g");
    g(x, y) = f(x + offset, y) + f(x - offset, y);

    // Provide estimates on the pipeline output
    g.set_estimates({{0, 1000}, {0, 1000}});

    // Provide estimates on the ImageParam
    input.set_estimates({{0, 1000}, {0, 1000}});

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // g.print_loop_nest();
}

void run_test_3() {
    Param<int> offset;
    offset.set_estimate(1);
    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");
    Func f("f");
    f(x, y) = input(x, y) * 2;

    Func output("output");
    output(x, y) = f(x + offset, y) + f(x - offset, y);

    // Provide estimates on the ImageParam
    input.set_estimates({{0, 1000}, {0, 1000}});

    // Provide estimates on the pipeline output,
    output.set_estimates({{0, 1000}, {0, 1000}});

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(output);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // output.print_loop_nest();
}

// Same as run_test_3, but with an output producing Tuples,
// thus we have multiple output buffers.
void run_test_4() {
    Param<int> offset;
    offset.set_estimate(1);
    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");
    Func f("f");
    f(x, y) = input(x, y) * 2;

    Func output("output");
    output(x, y) = Tuple(f(x + offset, y), f(x - offset, y));

    // Provide estimates on the ImageParam
    input.set_estimates({{0, 1000}, {0, 1000}});

    for (auto &output_buffer : output.output_buffers()) {
        output_buffer.set_estimates({{0, 1000}, {0, 1000}});
    }

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(output);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // output.print_loop_nest();
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

    std::cout << "Test 1:\n";
    run_test_1();
    std::cout << "Test 2:\n";
    run_test_2();
    std::cout << "Test 3:\n";
    run_test_3();
    std::cout << "Test 4:\n";
    run_test_4();
    printf("Success!\n");
    return 0;
}
