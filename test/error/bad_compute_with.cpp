#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y");

    f(x, y) = x + y;
	f(x, y) += 2;
	f.update(0).compute_with(f, x);

	f.realize(10, 10);

    return 0;
}
