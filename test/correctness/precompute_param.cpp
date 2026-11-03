#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>

using namespace Halide;

// A precomputed Func has to be constant: it can't read anything that's only
// known when the pipeline runs.

void param() {
    Var x("x");
    Param<int> p("p");
    Func lut("lut"), g("g");
    lut(x) = x + p;
    g(x) = lut(x % 8);
    lut.precompute();
    g.compile_to_module({p});
}

void image_param() {
    Var x("x");
    ImageParam im(Int(32), 1, "im");
    Func lut("lut"), g("g");
    lut(x) = im(x);
    g(x) = lut(x % 8);
    lut.precompute();
    g.compile_to_module({im});
}

void image_param_transitive() {
    Var x("x");
    ImageParam im(Int(32), 1, "im");
    Func b("b"), lut("lut"), g("g");
    b(x) = im(x) * 2;
    lut(x) = b(x) + 1;
    g(x) = lut(x % 8);
    lut.precompute();
    g.compile_to_module({im});
}

void input_buffer() {
    Var x("x");
    Buffer<int> in(8, "in");
    Func lut("lut"), g("g");
    lut(x) = in(x);
    g(x) = lut(x % 8);
    lut.precompute();
    g.compile_to_module({in});
}

void transitive() {
    Var x("x");
    Param<int> p("p");
    Func a("a"), lut("lut"), g("g");
    a(x) = x * p;
    lut(x) = a(x) + 1;
    g(x) = lut(x % 8);
    a.compute_root();
    lut.precompute();
    g.compile_to_module({p});
}

void rdom_bound() {
    Var x("x");
    Param<int> p("p");
    RDom r(0, p, "r");
    Func lut("lut"), g("g");
    lut(x) = 0;
    lut(x) += r;
    g(x) = lut(x % 8);
    lut.precompute();
    g.compile_to_module({p});
}

void extern_definition() {
    Var x("x");
    Func lut("lut"), g("g");
    lut.define_extern("make_lut", {}, Int(32), {x});
    g(x) = lut(x % 8);
    lut.precompute();
    g.compile_to_module({});
}

int main(int argc, char **argv) {
    if (!exceptions_enabled()) {
        printf("[SKIP] Halide was compiled without exceptions.\n");
        return 0;
    }
    int failures = 0;
    failures += !expect_user_error("param", "reads the Param \"p\"", param);
    failures += !expect_user_error("image_param", "but it reads the ImageParam \"im\"", image_param);
    failures += !expect_user_error("image_param_transitive", "the Func \"b\", which it depends on, reads the ImageParam \"im\"", image_param_transitive);
    failures += !expect_user_error("input_buffer", "reads the input Buffer \"in\"", input_buffer);
    failures += !expect_user_error("transitive", "the Func \"a\", which it depends on, reads the Param \"p\"", transitive);
    failures += !expect_user_error("rdom_bound", "reads the Param \"p\"", rdom_bound);
    failures += !expect_user_error("extern_definition", "extern definition", extern_definition);
    if (failures != 0) {
        printf("%d scenario(s) failed to produce the expected error\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
