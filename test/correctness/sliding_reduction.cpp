#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int counter = 0;
extern "C" int call_count(int x) {
    counter++;
    assert(counter > 0);
    return x;
}
HalideExtern_1(int, call_count, int);

int main(int argc, char **argv) {

    Func f("f");
    Var x, y;

    f(x, y) = x;

    f(0, y) += f(1, y) + f(0, y);

    f(x, y) = call_count(f(x, y));

    Func g("g");
    g(x, y) = f(x, y) + f(x, y-1) + f(x, y-2);

    f.store_root().compute_at(g, y);

    counter = 0;
    g.realize(2, 10);

    if (counter != 24) {
        printf("Failed sliding a reduction: %d\n", counter);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
