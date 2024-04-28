#include "Halide.h"

using namespace Halide;

int error_count = 0;
void my_error(JITUserContext *ucon, const char *msg) {
    error_count++;
}

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x");
    Param<int> p;

    f(x) = x;
    f(x) += 1;
    g(x) = f(x) + f(2 * x + p);

    g.vectorize(x, 8);
    f.bound_storage(x, 32);
    // No way to check this at compile time. The size of f depends on both x and
    // p.  An assert is injected, but the assert is inside g's vectorized loop.

    g.jit_handlers().custom_error = my_error;

    g.compile_jit();

    // Will trigger the assert
    p.set(256);
    g.realize({128});
    if (error_count != 1) {
        printf("There should have been an error\n");
        return 1;
    }

    // Will not trigger the assert
    p.set(0);
    g.realize({8});
    if (error_count != 1) {
        printf("There should not have been an error\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
