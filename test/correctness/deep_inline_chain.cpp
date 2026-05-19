#include "Halide.h"

using namespace Halide;

// Stress-test for inlining a Fibonacci-shaped chain of pure Funcs:
//   f_k(x) = f_{k-2}(f_{k-1}(x))
// Each f_k references its two predecessors, so naive per-call expansion of the
// call graph into the final IR is Fibonacci-sized — exponential in the chain
// length. The lowering pipeline must keep this within reason; this test exists
// to surface regressions that turn that into a crash or runaway compile.
int main(int argc, char **argv) {
    Var x, c;
    std::vector<Func> funcs;
    funcs.emplace_back(lambda(x, c, x + c));
    funcs.emplace_back(lambda(x, c, x + c + 1));
    funcs.emplace_back(lambda(x, c, x + c + 2));
    funcs.emplace_back(lambda(x, c, x + c + 3));
    for (int i = 0; i < 200; i++) {
        size_t j = funcs.size();
        Func next;
        Func lut;
        lut(x) = x * x + i;
        Expr e = 0;
        for (int k = 0; k < 10; k++) {
            int l = j - 1 - k;
            if (l < 0) break;
            Func &f = funcs[l];
            e += f(x, 0) * f(x, 1);
        }
        next(x, c) = lut(e);
        funcs.push_back(std::move(next));
    }

    funcs.back().compile_jit(Target{"host"});

    printf("Success!\n");
    return 0;
}
