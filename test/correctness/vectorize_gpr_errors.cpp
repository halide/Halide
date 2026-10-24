#include "Halide.h"
#include "expect_user_error.h"

#include <cstdio>
#include <functional>

using namespace Halide;

// Funcs stored in MemoryType::GPR whose vectorized loops contain
// something that can't be done lane-wise in a general-purpose register,
// or that can't be stored in general-purpose registers at all, should be
// rejected with an error that says what the problem is.

namespace {

template<typename T>
void compile_gpr(const std::function<Expr(Var, Func, Func)> &algorithm, int lanes, const char *name = "f") {
    ImageParam a(type_of<T>(), 1, "a"), c(type_of<T>(), 1, "c");
    Var x("x"), xo("xo"), xi("xi");
    Func fa("fa"), fc("fc"), f(name), out("out");
    fa(x) = a(x);
    fc(x) = c(x);
    f(x) = algorithm(x, fa, fc);
    out(x) = f(x);
    out.split(x, xo, xi, 32);
    f.compute_at(out, xo).vectorize(x, lanes).store_in(MemoryType::GPR);
    out.compile_jit();
}

}  // namespace

int main(int argc, char **argv) {
#ifndef HALIDE_WITH_EXCEPTIONS
    printf("[SKIP] vectorize_gpr_errors requires exceptions\n");
    return 0;
#else
    const int word_bits = get_jit_target_from_environment().bits;
    bool ok = true;

    ok &= expect_user_error("variable_shift", "a shift by an amount that varies across the lanes", [] {
        compile_gpr<uint8_t>([](Var x, Func a, Func c) { return a(x) >> c(x); }, 4);
    });

    ok &= expect_user_error("multiply", "a multiplication", [] {
        compile_gpr<uint8_t>([](Var x, Func a, Func c) { return a(x) * c(x); }, 4);
    });

    ok &= expect_user_error("divide", "a division", [] {
        compile_gpr<uint8_t>([](Var x, Func a, Func c) { return a(x) / 3; }, 4);
    });

    ok &= expect_user_error("gather", "a gather", [] {
        compile_gpr<uint8_t>([](Var x, Func a, Func c) { return a(cast<int>(c(x))); }, 4);
    });

    ok &= expect_user_error("float_lanes", "does not have integer lanes", [] {
        compile_gpr<float>([](Var x, Func a, Func c) { return a(x) + c(x); }, 2);
    });

    ok &= expect_user_error("too_wide_u8", "which does not fit in a", [=] {
        compile_gpr<uint8_t>([](Var x, Func a, Func c) { return a(x) ^ c(x); }, word_bits / 8 * 2);
    });

    ok &= expect_user_error("too_wide_u32", "which does not fit in a", [=] {
        compile_gpr<uint32_t>([](Var x, Func a, Func c) { return a(x) ^ c(x); }, word_bits / 32 * 2);
    });

    ok &= expect_user_error("not_power_of_two", "not a power of two bits wide", [] {
        compile_gpr<uint8_t>([](Var x, Func a, Func c) { return a(x) ^ c(x); }, 3);
    });

    ok &= expect_user_error("the_func_is_named", "Func my_gpr_func is stored in MemoryType::GPR", [] {
        compile_gpr<uint8_t>([](Var x, Func a, Func c) { return a(x) * c(x); }, 4, "my_gpr_func");
    });

    ok &= expect_user_error("update_stage", "Func update_f is stored in MemoryType::GPR, so the vectorized loop update_f.s1", [] {
        Func f("update_f"), out("out");
        Var x("x");
        f(x) = cast<uint8_t>(x);
        f(x) = f(x) * f(x);
        out(x) = f(x);
        out.bound(x, 0, 16);
        f.compute_root().store_in(MemoryType::GPR);
        f.update().vectorize(x, 4);
        out.compile_jit();
    });

    ok &= expect_user_error("simd_loop_stores_to_gpr", "which is stored in MemoryType::GPR, and g, which is not", [] {
        Func f("f"), g("g");
        Var x("x"), xo("xo"), xi("xi");
        f(x) = cast<uint8_t>(x);
        g(x) = f(x) + 1;
        g.split(x, xo, xi, 4).vectorize(xi);
        f.compute_at(g, xi).store_in(MemoryType::GPR);
        g.compile_jit();
    });

    ok &= expect_user_error("dynamic_size", "Allocations in MemoryType::GPR must have a constant size", [] {
        Func f("f"), g("g");
        Var x("x");
        f(x) = cast<uint8_t>(x);
        g(x) = f(x) + 1;
        f.compute_root().vectorize(x, 4).store_in(MemoryType::GPR);
        g.compile_jit();
    });

    ok &= expect_user_error("output", "but it is an output of the pipeline", [] {
        Func f("f");
        Var x("x");
        f(x) = cast<uint8_t>(x);
        f.vectorize(x, 4).store_in(MemoryType::GPR);
        f.compile_jit();
    });

    ok &= expect_user_error("buffer_param", "can't be stored in MemoryType::GPR", [] {
        ImageParam p(UInt(8), 1, "p");
        p.store_in(MemoryType::GPR);
    });

    ok &= expect_user_error("gpu", "only supported for Funcs computed and used on the host CPU", [] {
        Func f("f"), g("g");
        Var x("x"), xo("xo"), xi("xi");
        f(x) = cast<uint8_t>(x);
        g(x) = f(x) + 1;
        g.gpu_tile(x, xo, xi, 64);
        f.compute_at(g, xi).store_in(MemoryType::GPR);
        g.compile_to_module({}, "g", get_host_target().with_feature(Target::CUDA));
    });

    ok &= expect_user_error("used_on_gpu", "is used in code that runs on device API", [] {
        Func f("f"), g("g");
        Var x("x"), xo("xo"), xi("xi");
        f(x) = cast<uint8_t>(x);
        g(x) = f(x) + 1;
        g.bound(x, 0, 64).gpu_tile(x, xo, xi, 16);
        f.compute_root().vectorize(x, 4).store_in(MemoryType::GPR);
        g.compile_to_module({}, "g", get_host_target().with_feature(Target::CUDA));
    });

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
#endif
}
