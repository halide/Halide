#include "Halide.h"
#include <stdio.h>

using namespace Halide;
Expr calc(Expr a) {
    Expr prod = cast<int64_t>(a) * cast<int64_t>(a);
    Expr scaled = (prod + (1 << 30)) >> 31;
    Expr clamped = clamp(scaled, Int(32).min(), Int(32).max());
    return cast<int32_t>(clamped);
}

int main(int argc, char **argv) {
    // LLVM 21 calls getFixedValue() on scalable TypeSize objects in the
    // AArch64 backend, triggering an assertion. Fixed in LLVM 22 by:
    // https://github.com/llvm/llvm-project/commit/d1500d12be60 (PR #169764)
    if (Internal::get_llvm_version() < 220 &&
        get_jit_target_from_environment().has_feature(Target::SVE2)) {
        printf("[SKIP] LLVM 21 has known getFixedValue() assertion failures on SVE scalable types.\n");
        return 0;
    }

    Var x, y;
    Func f, g, h;

    // Construct a series of operations that will trigger a signed_integer_overflow
    // during simplification
    f(x, y) = calc(max(x + (1 << 28), -(1 << 29) + (1 << 28)));
    g(x, y) = calc(f(x, y)) + f(x, y) / 4 + (1 << 30);
    h(x, y) = calc(g(x, y)) + g(x, y) / 4 + (1 << 30);
    h.vectorize(x, 8).compute_root();

    Buffer<int32_t> imf = h.realize({32, 32});

    // No verification of output: just want to verify no compile-time assertion

    printf("Success!\n");
    return 0;
}
