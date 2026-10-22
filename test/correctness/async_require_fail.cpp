#include "Halide.h"
#include "expect_user_error.h"
#include <memory>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not yet support async().\n");
        return 0;
    }

    return error_test("async_require_fail", "The parameters should add to exactly 7829 but were 1 2", []() {
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
        result = g.realize({1});
        (void)result;
    });
}
