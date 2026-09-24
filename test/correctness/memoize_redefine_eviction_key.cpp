#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("memoize_redefine_eviction_key", "Can't redefine memoize eviction key. First definition is:", []() {
        Param<float> val;

        Func f, g;
        Var x, y;

        f(x, y) = val + cast<uint8_t>(x);
        g(x, y) = f(x, y) + f(x - 1, y) + f(x + 1, y);

        f.compute_root().memoize(EvictionKey(42));
        f.compute_root().memoize(EvictionKey(1764));
    });
}
