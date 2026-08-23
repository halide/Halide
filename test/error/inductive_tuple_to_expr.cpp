#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func g = Func(std::vector<Type>{Int(32), Int(32)}, "g");
    Func h("h");
    Var x("x"), y("y");

    g(x, y) = Tuple(select(x <= 0, 0, g(max(0, x - 1), y) + x + y), select(y <= 0, 0, g(x, max(0, y - 1)) + x + y));

    h(x, y) = g(x, y)[0] / 2;

    h.realize({10, 10});

    printf("Success!\n");
    return 0;
}
