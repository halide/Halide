#include "Halide.h"
#include "expect_user_error.h"
#include <memory>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("require_fail", "The parameters should add to exactly 7829 but were 1 2", []() {
        const int kPrime1 = 7829;
        const int kPrime2 = 7919;

        Buffer<int> result;
        Param<int> p1, p2;
        Var x;
        Func f;
        f(x) = require((p1 + p2) == kPrime1,
                       (p1 + p2) * kPrime2,
                       "The parameters should add to exactly", kPrime1, "but were", p1, p2);
        // choose values that will fail
        p1.set(1);
        p2.set(2);
        result = f.realize({1});
        (void)result;
    });
}
