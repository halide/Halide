#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>

using namespace Halide;

void output() {
    Var x("x");
    Func f("f");
    f(x) = x;
    f.precompute();
    f.compile_to_module({});
}

void output_jit() {
    Var x("x");
    Func f("f");
    f(x) = x;
    f.precompute();
    f.realize({8});
}

void extern_consumer() {
    Var x("x");
    Func lut("lut"), e("e"), g("g");
    lut(x) = x;
    e.define_extern("consume_lut", {lut}, Int(32), {x});
    g(x) = e(x);
    lut.precompute();
    e.compute_root();
    g.compile_to_module({});
}

int main(int argc, char **argv) {
    if (!exceptions_enabled()) {
        printf("[SKIP] Halide was compiled without exceptions.\n");
        return 0;
    }
    int failures = 0;
    failures += !expect_user_error("output", "precomputed output Func", output);
    failures += !expect_user_error("output_jit", "precomputed output Func", output_jit);
    failures += !expect_user_error("extern_consumer", "consumed by the extern stage \"e\"", extern_consumer);
    if (failures != 0) {
        printf("%d scenario(s) failed to produce the expected error\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
