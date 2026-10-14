#include "Halide.h"

#include <iostream>

using namespace Halide;

int main() {
    Var x("x"), y("y");
    Func input("input"), gain("gain"), output("output");

    input(x, y) = x + y;
    gain(x, y) = input(x, y) * 3;
    output(x, y) = gain(x, y) + 1;

    // Add a CPU schedule with outer y parallelism, fixed-size y strips, SIMD
    // across stride-1 x, and no unnecessary root-sized temporary for gain.

    output.print_loop_nest();
    Buffer<int> result = output.realize({127, 65});
    if (result(126, 64) != (126 + 64) * 3 + 1) {
        std::cerr << "incorrect result\n";
        return 1;
    }
    std::cout << "Success!\n";
    return 0;
}
