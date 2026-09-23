#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>
#include <string>

using namespace Halide;

void axis_collision() {
    Func f("f");
    Var x("x"), y("y"), xi("xi");

    f(x, y) = x + y;
    f.split_storage(x, y, xi, 4);
}

void factor_type() {
    Func f("f");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x;
    f.split_storage(x, xo, xi, 4.0f);
}

void fold_axis() {
    Func f("f"), g("g");
    Var x("x"), y("y"), xi("xi");

    f(x, y) = x + y;
    g(x, y) = f(x, y) + f(x + 1, y);
    f.compute_at(g, y)
        .store_root()
        .split_storage(x, x, xi, 4)
        .fold_storage(x, 4);

    g.realize({16, 16});
}

void output() {
    Func f("f");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x;
    f.split_storage(x, xo, xi, 4);
    f.realize({16});
}

void prefetch() {
    Func f("f"), g("g");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x;
    g(x) = f(x);
    f.compute_root().split_storage(x, xo, xi, 4);
    g.prefetch(f, x, x, 8);

    g.realize({16});
}

void extern_consumer() {
    Func f("f"), g("g");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x;
    f.compute_root().split_storage(x, xo, xi, 4);
    g.define_extern("extern_consumer", {f}, Int(32), {x});

    g.compile_to_module({});
}

void extern_producer() {
    Func f("f");
    Var x("x"), xo("xo"), xi("xi");

    f.define_extern("extern_producer", {}, Int(32), {x});
    f.split_storage(x, xo, xi, 4);
}

void pre_split_setting() {
    Func f("f");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x;
    f.bound_storage(x, 16).split_storage(x, xo, xi, 4);
}

bool nonpositive_factor() {
    try {
        Func f("f"), g("g");
        Var x("x"), xo("xo"), xi("xi");
        Param<int> factor("factor");

        f(x) = x;
        g(x) = f(x);
        f.compute_root().split_storage(x, xo, xi, factor);

        factor.set(0);
        g.realize({16});
    } catch (const RuntimeError &e) {
        const std::string msg = e.what();
        if (msg.find("not strictly positive") == std::string::npos) {
            printf("[nonpositive_factor] FAIL: unexpected runtime error:\n%s\n", msg.c_str());
            return false;
        }
        printf("[nonpositive_factor] OK: %s\n", msg.c_str());
        return true;
    } catch (...) {
        printf("[nonpositive_factor] FAIL: expected a RuntimeError but got a different exception\n");
        return false;
    }
    printf("[nonpositive_factor] FAIL: expected a runtime error but none was raised\n");
    return false;
}

int main(int argc, char **argv) {
    if (!exceptions_enabled()) {
        printf("[SKIP] Halide was compiled without exceptions.\n");
        return 0;
    }

    int failures = 0;
    failures += !expect_user_error("axis_collision", "already used", axis_collision);
    failures += !expect_user_error("extern_consumer", "consumed by the extern stage", extern_consumer);
    failures += !expect_user_error("extern_producer", "has an extern definition", extern_producer);
    failures += !expect_user_error("factor_type", "not representable as int32", factor_type);
    failures += !expect_user_error("fold_axis", "split_storage axis", fold_axis);
    failures += !nonpositive_factor();
    failures += !expect_user_error("output", "only supported for internal allocations", output);
    failures += !expect_user_error("pre_split_setting", "Apply these to the split axes", pre_split_setting);
    failures += !expect_user_error("prefetch", "prefetch is not supported", prefetch);

    if (failures != 0) {
        printf("%d scenario(s) failed to produce the expected error\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
