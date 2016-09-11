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

    for (int y = 0; y < 10; y++) {
      for (int x = 0; x < 10; x++) {
	printf("%d ", result(x, y));
      }
      printf("\n");
    }
    return 0;
}

