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
    for (int i = 0; i < 5000; i++) {
        exprs.push_back(x & i);
    }

    f(x) = mux(x, exprs);

    f.bound(x, 0, (int)exprs.size());
    f.unroll(x);

    f.compile_jit();

    printf("Success!\n");
    return 0;
}
