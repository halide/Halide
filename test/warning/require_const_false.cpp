#include "Halide.h"
#include <memory>
#include <stdio.h>

using namespace Halide;

void halide_error(void *ctx, const char *msg) {
    // Emitting "error.*:" to stdout or stderr will cause CMake to report the
    // test as a failure on Windows, regardless of error code returned,
    // hence the abbreviation to "err".
    printf("Saw (Expected) Halide Err: %s\n", msg);
}

int main(int argc, char **argv) {
    const int kPrime1 = 7829;
    const int kPrime2 = 7919;

    Buffer<int> result;
    Var x;
    Func f;
    // choose values that will simplify the require() condition to const-false
    Expr p1 = 1;
    Expr p2 = 2;
    f(x) = require((p1 + p2) == kPrime1,
                   (p1 + p2) * kPrime2,
                   "The parameters should add to exactly", kPrime1, "but were", p1, p2);
    f.set_error_handler(&halide_error);
    result = f.realize(1);

    return 0;
}
