#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y"), xi("xi");

    f(x, y) = x + y;
    f.split_storage(x, y, xi, 4);

    return 0;
}
