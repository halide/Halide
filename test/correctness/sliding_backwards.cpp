#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int call_counter = 0;
extern "C" HALIDE_EXPORT_SYMBOL int count(int arg) {
    call_counter++;
    return arg;
}
HalideExtern_1(int, count, int);

int main(int argc, char **argv) {
    Func f, g;
    Var x;

    g(x) = count(x);
    f(x) = g(100 - x) + g(100 - x + 1);

    g.compute_at(f, x);
    g.store_root();

    f.realize({10});

    if (call_counter != 11) {
        printf("g was called %d times instead of %d\n", call_counter, 11);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
