#include "Halide.h"
#include <stdio.h>
#include <memory>

int error_occurred = false;
void halide_error(void *ctx, const char *msg) {
    printf("Saw Halide Error: %s\n", msg);
    error_occurred = true;
}

using namespace Halide;

int main(int argc, char **argv) {
    // Use something other than 'int' here to ensure that the return
    // type of the error-handler path doesn't need to match the expr type
    const float kPrime1 = 7829.f;
    const float kPrime2 = 7919.f;

    Buffer<float> result;
    Param<float> p1, p2;
    Var x;
    Func f;
    f(x) = require((p1 + p2) == kPrime1, 
                   (p1 + p2) * kPrime2,
                   "The parameters should add to exactly", (kPrime1 * kPrime2), "but were", p1, p2);
    f.set_error_handler(&halide_error);

    // It should be the case that the non-error path of the code
    // assumes (p1 + p2) == kPrime1, and thus hardcodes the body to fill
    // in the result to the constant kPrime1*kPrime2 (rather than
    // actually computing the result at runtime).
    // f.compile_to_assembly("require_.s", {p1, p2}, "require_body");

    p1.set(1);
    p2.set(2);
    error_occurred = false;
    result = f.realize(1);
    if (!error_occurred) {
        printf("There should have been a requirement error\n");
        return 1;
    }


    p1.set(1);
    p2.set(kPrime1-1);
    error_occurred = false;
    result = f.realize(1);
    if (error_occurred) {
        printf("There should not have been a requirement error\n");
        return 1;
    }
    if (result(0) != (kPrime1 * kPrime2)) {
        printf("Unexpected value: %f\n", result(0));
        return 1;
    }

    printf("Success!\n");
    return 0;

}
