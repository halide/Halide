#include "Halide.h"
#include <memory>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    const Target t = get_jit_target_from_environment();
    if (t.arch == Target::WebAssembly && !t.has_feature(Target::WasmThreads)) {
        printf("[SKIP] WebAssembly does not support async() without wasm_threads enabled.\n");
        _halide_user_assert(0);
    }

    const int kPrime1 = 7829;
    const int kPrime2 = 7919;

    Buffer<int> result;
    Param<int> p1, p2;
    Var x;
    Func f, g;
    f(x) = require((p1 + p2) == kPrime1,
                   (p1 + p2) * kPrime2,
                   "The parameters should add to exactly", kPrime1, "but were", p1, p2);
    g(x) = f(x) + f(x + 1);
    f.compute_at(g, x).async();
    // choose values that will fail
    p1.set(1);
    p2.set(2);
    result = g.realize(1);

    printf("Success!\n");
    return 0;
}
