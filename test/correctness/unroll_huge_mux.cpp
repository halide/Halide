#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
#ifdef HALIDE_INTERNAL_USING_ASAN
    printf("[SKIP] unroll_huge_mux requires set_compiler_stack_size() to work properly, which is disabled under ASAN.\n");
    return 0;
#endif

    Func f;
    Var x;

    std::vector<Expr> exprs;
    for (int i = 0; i < 10000; i++) {
        exprs.push_back(x & i);
    }

    f(x) = mux(x, exprs);

    f.bound(x, 0, (int)exprs.size());
    f.unroll(x);

    // For 10000 expressions in the mux, this test uses more than 8MB
    // in stack because the simplifier's Block visitor is still
    // recursive and has a large stack frame. We'll put a cap on it to
    // at least make sure the problem doesn't get worse. If this test
    // crashes try raising the cap to see if we have a stack size
    // regression.
    //
    // https://github.com/halide/Halide/issues/6238

    set_compiler_stack_size(16 * 1024 * 1024);

    f.compile_jit();

    printf("Success!\n");
    return 0;
}
