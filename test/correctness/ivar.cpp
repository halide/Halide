#include <cassert>
#include <stdio.h>
#include "Halide.h"
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    IVar x_implicit("x_implicit"), y_implicit("y_implicit");

    Func input("input"), f("f"), g("g"), h("h"), output("output");

    input(x, y) = x + y * 256;

    f() = input(x_implicit, y_implicit);

    g() = f() + 42;
    h(x, y) = input(x, y) * 2;
    output(x_implicit, y_implicit) = g() + h(x_implicit, y_implicit);

    Image<int32_t> result = output.realize(10, 10);
    // TODO: change this to assertions
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            printf("%d ", result(x, y));
        }
        printf("\n");
    }

    Func hist_in("hist_in");
    hist_in(x, y) = cast<uint8_t>(x + 3 * x_implicit + 5 * (y + 3 * y_implicit)) & ~1;

    Var bin;
    Func histogram("histogram");
    RDom range(0, 3, 0, 3);
    histogram(hist_in(range.x, range.y)) += 1;
    histogram.compute_root();

    Func output2("output2");
    output2(x_implicit, y_implicit, bin) = histogram(bin);
    Image<int32_t> hists = output2.realize(2, 2, 31);

    // TODO: change this to assertions
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
	    for (int bin = 0; bin < 31; bin++) {
	        printf("%d ", hists(x, y, bin));
	    }
	    printf("\n");
	}
    }

    // TODO: Add more tests. Notably something with define_extern

    printf("Success!\n");
    return 0;
}
