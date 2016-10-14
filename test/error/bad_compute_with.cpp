#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y"), t("t");

    f(x, y) = x + y;
	f(x, y) += 2;
	f.rename(y, t);
	f.update(0).rename(x, t).reorder(y, t);
	f.update(0).compute_with(f, t);

	f.realize(10);

    return 0;
}
