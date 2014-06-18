#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x");
    Func f("f"), g("g");

    f(x) = print(x * x);
    Image<int32_t> result = f.realize(10);

    for (int32_t i = 0; i < 10; i++) {
        if (result(i) != i * i) {
            return -1;
        }
    }

    g(x) = print_when(x == 3, x * x, "g");
    result = g.realize(10);

    for (int32_t i = 0; i < 10; i++) {
        if (result(i) != i * i) {
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
