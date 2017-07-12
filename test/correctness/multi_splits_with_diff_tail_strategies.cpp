#include "Halide.h"
#include "test/common/check_call_graphs.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // ApplySplit should respect the order of the application of substitutions/
    // predicates/lets; otherwise, this combination of tail strategies will
    // cause an access out of bound error.
    Func f("f"), input("input");
    Var x("x"), y("y"), c("c");

    f(x, y, c) = x + y + c;

    f.reorder(c, x, y);
    Var yo("yo"), yi("yi");
    f.split(y, yo, yi, 2, TailStrategy::RoundUp);

    Var yoo("yoo"), yoi("yoi");
    f.split(yo, yoo, yoi, 64, TailStrategy::GuardWithIf);

    Buffer<int> im = f.realize(3000, 2000, 3);
    auto func = [](int x, int y, int c) {
        return x + y + c;
    };
    if (check_image(im, func)) {
        return -1;
    }

    return 0;
}
