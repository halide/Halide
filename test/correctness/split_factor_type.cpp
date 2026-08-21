#include "Halide.h"
#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    // Split factors with types not representable as int32 (e.g. uint32)
    // should be rejected up front with a clean error, not produce an
    // internal compiler error during lowering. See issue #9143.

    // Types that fit in int32 should still work.
    {
        Var x, xo, xi;
        Func f("f");
        f(x) = x;
        f.bound(x, 0, 64);
        f.split(x, xo, xi, Expr((int16_t)8));
        f.compile_jit();
    }
    {
        Var x, xo, xi;
        Func f("g");
        f(x) = x;
        f.bound(x, 0, 64);
        f.split(x, xo, xi, Expr((uint16_t)8));
        f.compile_jit();
    }

#if HALIDE_WITH_EXCEPTIONS
    // A uint32 split factor is not representable as int32, so it should
    // be rejected at split() time. Only check this if exceptions are
    // available at runtime, so we can recover from the error.
    if (Halide::exceptions_enabled()) {
        Var x, xo, xi;
        Func f("h");
        f(x) = x;
        f.bound(x, 0, 64);

        bool error = false;
        try {
            f.split(x, xo, xi, Expr((uint32_t)8));
        } catch (const Halide::CompileError &e) {
            error = true;
            printf("Expected compile error:\n%s\n", e.what());
        }
        if (!error) {
            printf("There was supposed to be an error!\n");
            return 1;
        }
    }
#endif

    printf("Success!\n");
    return 0;
}
