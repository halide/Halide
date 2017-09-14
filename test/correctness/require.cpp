#include "Halide.h"
#include <stdio.h>
#include <memory>

int error_occurred = false;
void halide_error(void *ctx, const char *msg) {
    printf("Saw (Expected) Halide Error: %s\n", msg);
    error_occurred = true;
}

using namespace Halide;

static void test(int vector_width) { 
    const int32_t kPrime1 = 7829;
    const int32_t kPrime2 = 7919;

    int32_t realize_width = vector_width ? vector_width : 1;

    Buffer<int32_t> result;
    Param<int32_t> p1, p2;
    Var x;
    Func s, f;
    s(x) = p1 + p2;
    f(x) = require(s(x) == kPrime1, 
                   s(x) * kPrime2 + x,
                   "The parameters should add to exactly", kPrime1, "but were", s(x), "for vector_width", vector_width);
    if (vector_width) {
        s.vectorize(x, vector_width).compute_root();
        f.vectorize(x, vector_width);
    }
    f.set_error_handler(&halide_error);

    // choose values that will fail
    p1.set(1);
    p2.set(2);
    error_occurred = false;
    result = f.realize(realize_width);
    if (!error_occurred) {
        printf("There should have been a requirement error (vector_width = %d)\n", vector_width);
        exit(1);
    }

    p1.set(1);
    p2.set(kPrime1-1);
    error_occurred = false;
    result = f.realize(realize_width);
    if (error_occurred) {
        printf("There should not have been a requirement error (vector_width = %d)\n", vector_width);
        exit(1);
    }
    for (int i = 0; i < realize_width; ++i) {
        const int32_t expected = (kPrime1 * kPrime2) + i;
        const int32_t actual = result(i);
        if (actual != expected) {
            printf("Unexpected value at %d: actual=%d, expected=%d (vector_width = %d)\n", i, actual, expected, vector_width);
            exit(1);
        }
    }
}

int main(int argc, char **argv) {
    test(0);
    test(4);
    printf("Success!\n");
    return 0;

}
