#include "Halide.h"
#include <stdio.h>

// An extern stage that translates.
extern "C" HALIDE_EXPORT_SYMBOL int translate(halide_buffer_t *in, int dx, int dy, halide_buffer_t *out) {

    if (in->is_bounds_query()) {
        in->dim[0].min = out->dim[0].min + dx;
        in->dim[1].min = out->dim[1].min + dy;
        in->dim[0].extent = out->dim[0].extent;
        in->dim[1].extent = out->dim[1].extent;
    } else {
        Halide::Runtime::Buffer<uint8_t> out_buf(*out);
        out_buf.translate(dx, dy);
        out_buf.copy_from(Halide::Runtime::Buffer<uint8_t>(*in));
    }

    return 0;
}

using namespace Halide;

// Test a pipe with several extern-defined Funcs.
void test_case_1() {
    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");
    Func f0("f0");
    f0(x, y) = input(x, y) * 2;

    Func f1("f1"), f2("f2");
    std::vector<ExternFuncArgument> args(3);
    args[0] = f0;
    args[1] = Expr(3);
    args[2] = Expr(7);

    f1.define_extern("translate", args, UInt(8), 2);

    args[1] = Expr(8);
    args[2] = Expr(17);
    f2.define_extern("translate", args, UInt(8), 2);

    Func g("g");
    g(x, y) = f1(x, y) + f2(x, y);

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

// Test with an extern Func which consumes a trivial Func; autoscheduler
// should not attempt to inline into the extern consumer.
void test_case_2() {
    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");
    Func f0("f0"), f1("f1"), f2("f2"), g("g");

    f0(x, y) = input(x, y) * 2;

    // Create f1, which is not a wrapper, but is trivial to inline
    // into the next extern Func (because print() has no cost)
    f1(x, y) = print(f0(x, y));

    f2.define_extern("translate", {f1, Expr(0), Expr(0)}, UInt(8), 2);

    g(x, y) = f2(x, y);

    g.set_estimates({{0, 10}, {0, 10}});
    input.set_estimates({{0, 10}, {0, 10}});

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // g.print_loop_nest();
}

// Test with an extern Func that consumes a non-pure Func.
// The autoscheduler will have to deal properly with scheduling
// the non-pure Func non-inlined even though it is unbounded.
void test_case_3() {

    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");
    Func f0("f0"), f1("f1"), f2("f2"), g("g");

    f0(x, y) = input(x, y) * 2;

    // make f1, which is a sum (not pure).
    RDom r(0, 2, "r");
    f1(x, y) = sum(f0(x + r.x, y));

    f2.define_extern("translate", {f1, Expr(0), Expr(0)}, UInt(8), 2);

    g(x, y) = f2(x, y);

    g.set_estimates({{0, 10}, {0, 10}});
    input.set_estimates({{0, 10}, {0, 10}});

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // g.print_loop_nest();
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

    test_case_1();
    test_case_2();
    test_case_3();

    printf("Success!\n");
    return 0;
}
