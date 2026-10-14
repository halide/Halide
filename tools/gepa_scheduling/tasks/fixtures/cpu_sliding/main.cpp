#include "Halide.h"

#include <cmath>
#include <iostream>

using namespace Halide;

int main() {
    Var x("x"), y("y");
    Func source("source"), blur_y("blur_y"), blur_x("blur_x"), output("output");

    source(x, y) = cast<float>(x + 2 * y);
    blur_y(x, y) = (source(x, y - 1) + source(x, y) + source(x, y + 1)) / 3.0f;
    blur_x(x, y) = (blur_y(x - 1, y) + blur_y(x, y) + blur_y(x + 1, y)) / 3.0f;
    output(x, y) = blur_x(x, y);

    // Replace this schedule with one parallel output region, SIMD on x, and
    // producer storage outside producer computation to enable sliding reuse.
    blur_y.compute_root();
    blur_x.compute_root();

    output.print_loop_nest();
    Buffer<float> result = output.realize({128, 128});
    const float expected = (64 + 2 * 64);
    if (std::abs(result(64, 64) - expected) > 0.001f) {
        std::cerr << "incorrect result\n";
        return 1;
    }
    std::cout << "Success!\n";
    return 0;
}
