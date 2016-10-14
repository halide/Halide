#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");

    f(x, y) = x + y;
	f(x, y) += 2;

	f.split(x, xo, xi, 2, TailStrategy::ShiftInwards);
	f.update(0).split(x, xo, xi, 2, TailStrategy::GuardWithIf);

	f.split(xo, xoo, xoi, 2, TailStrategy::GuardWithIf);
	f.update(0).split(xo, xoo, xoi, 2, TailStrategy::GuardWithIf);

	f.update(0).compute_with(f, xoo);

	f.realize(10);

    return 0;
}
