#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>

using namespace Halide;

void compute_at() {
    Var x("x");
    Func lut("lut"), g("g");
    lut(x) = x;
    g(x) = lut(x % 8);
    lut.precompute().compute_at(g, x);
    g.compile_to_module({});
}

void compute_inline() {
    Var x("x");
    Func lut("lut"), g("g");
    lut(x) = x;
    g(x) = lut(x % 8);
    lut.precompute().compute_inline();
    g.compile_to_module({});
}

void memoized() {
    Var x("x");
    Func lut("lut"), g("g");
    lut(x) = x;
    g(x) = lut(x % 8);
    lut.precompute().memoize();
    g.compile_to_module({});
}

int main(int argc, char **argv) {
    if (!exceptions_enabled()) {
        printf("[SKIP] Halide was compiled without exceptions.\n");
        return 0;
    }
    int failures = 0;
    failures += !expect_user_error("compute_at", "must be computed at root", compute_at);
    failures += !expect_user_error("compute_inline", "scheduled to be inlined", compute_inline);
    failures += !expect_user_error("memoized", "both precompute() and memoize()", memoized);
    if (failures != 0) {
        printf("%d scenario(s) failed to produce the expected error\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
