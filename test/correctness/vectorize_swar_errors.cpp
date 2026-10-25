#include "Halide.h"
#include "expect_user_error.h"

#include <cstdio>
#include <functional>

using namespace Halide;

// Schedules that vectorize with VectorizeIn::SWAR a loop containing
// something that can't be done lane-wise in a general-purpose register
// should be rejected with an error that says what the problem is.

namespace {

template<typename T>
void compile_swar(const std::function<Expr(Var, Func, Func)> &algorithm, int lanes, const char *name = "f") {
    ImageParam a(type_of<T>(), 1, "a"), c(type_of<T>(), 1, "c");
    Var x("x");
    Func fa("fa"), fc("fc"), f(name);
    fa(x) = a(x);
    fc(x) = c(x);
    f(x) = algorithm(x, fa, fc);
    f.vectorize(x, lanes, VectorizeIn::SWAR);
    f.compile_jit();
}

}  // namespace

int main(int argc, char **argv) {
#ifndef HALIDE_WITH_EXCEPTIONS
    printf("[SKIP] vectorize_swar_errors requires exceptions\n");
    return 0;
#else
    const int word_bits = get_jit_target_from_environment().bits;
    bool ok = true;

    ok &= expect_user_error("variable_shift", "a shift by an amount that varies across the lanes", [] {
        compile_swar<uint8_t>([](Var x, Func a, Func c) { return a(x) >> c(x); }, 4);
    });

    ok &= expect_user_error("multiply", "a multiplication", [] {
        compile_swar<uint8_t>([](Var x, Func a, Func c) { return a(x) * c(x); }, 4);
    });

    ok &= expect_user_error("divide", "a division", [] {
        compile_swar<uint8_t>([](Var x, Func a, Func c) { return a(x) / 3; }, 4);
    });

    ok &= expect_user_error("gather", "a gather", [] {
        compile_swar<uint8_t>([](Var x, Func a, Func c) { return a(cast<int>(c(x))); }, 4);
    });

    ok &= expect_user_error("float_lanes", "does not have integer lanes", [] {
        compile_swar<float>([](Var x, Func a, Func c) { return a(x) + c(x); }, 2);
    });

    ok &= expect_user_error("too_wide_u8", "which does not fit in a", [=] {
        compile_swar<uint8_t>([](Var x, Func a, Func c) { return a(x) ^ c(x); }, word_bits / 8 * 2);
    });

    ok &= expect_user_error("too_wide_u32", "which does not fit in a", [=] {
        compile_swar<uint32_t>([](Var x, Func a, Func c) { return a(x) ^ c(x); }, word_bits / 32 * 2);
    });

    ok &= expect_user_error("not_power_of_two", "not a power of two bits wide", [] {
        compile_swar<uint8_t>([](Var x, Func a, Func c) { return a(x) ^ c(x); }, 3);
    });

    ok &= expect_user_error("the_func_is_named", "In the schedule for Func my_swar_func,", [] {
        compile_swar<uint8_t>([](Var x, Func a, Func c) { return a(x) * c(x); }, 4, "my_swar_func");
    });

    ok &= expect_user_error("update_stage", "In the schedule for Func update_f, loop update_f.s1", [] {
        Func f("update_f");
        Var x("x");
        f(x) = cast<uint8_t>(x);
        f(x) = f(x) * f(x);
        f.bound(x, 0, 16);
        f.update().vectorize(x, 4, VectorizeIn::SWAR);
        f.compile_jit();
    });

    ok &= expect_user_error("nested_simd_in_swar", "can't be nested inside a SWAR-vectorized one", [] {
        Func f("f");
        Var x("x"), y("y");
        f(x, y) = cast<uint8_t>(x + y);
        f.vectorize(y, 2, VectorizeIn::SWAR).vectorize(x, 2);
        f.compile_jit();
    });

    ok &= expect_user_error("gpu", "only supported for code that runs on the host CPU", [] {
        Func f("f");
        Var x("x"), xo("xo"), xi("xi"), xii("xii");
        f(x) = cast<uint8_t>(x);
        f.split(x, xo, xi, 64)
            .split(xi, xi, xii, 4)
            .gpu_blocks(xo)
            .gpu_threads(xi)
            .vectorize(xii, 4, VectorizeIn::SWAR);
        f.compile_to_module({}, "f", get_host_target().with_feature(Target::CUDA));
    });

    ok &= expect_user_error("gpr_storage_too_wide", "does not fit in a", [=] {
        Func f("f"), g("g");
        Var x("x");
        f(x) = cast<uint8_t>(x);
        g(x) = f(x) + f(x + 1);
        g.bound(x, 0, 64);
        f.compute_root().store_in(MemoryType::GPR).vectorize(x, word_bits / 8 * 2);
        g.compile_jit();
    });

    ok &= expect_user_error("gpr_storage_dynamic_size", "has a dynamic size", [] {
        ImageParam in(UInt(8), 1, "in");
        Func f("f"), g("g");
        Var x("x");
        f(x) = in(x);
        g(x) = f(x);
        f.compute_root().store_in(MemoryType::GPR);
        g.compile_jit();
    });

    ok &= expect_user_error("gpr_storage_gpu", "only supported on CPU targets", [] {
        Func f("f"), g("g");
        Var x("x"), xo("xo"), xi("xi");
        f(x) = cast<uint8_t>(x);
        g(x) = f(x) * 2;
        g.gpu_tile(x, xo, xi, 16);
        f.compute_at(g, xi).store_in(MemoryType::GPR);
        g.compile_to_module({}, "g", get_host_target().with_feature(Target::CUDA));
    });

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
#endif
}
