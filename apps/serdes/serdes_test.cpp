#include <Halide.h>
#include "Serializer.h"

using namespace Halide;

int main(int argc, char **argv) {
    // First we'll declare some Vars to use below.
    Var x("x"), y("y"), c("c");
    Var p, q, m;
    // Now we'll express a multi-stage pipeline that blurs an image
    // first horizontally, and then vertically.
    {
        Func input("input");

        Expr e = p + q + m;

        input(p, q, m) = e;
        // Upgrade it to 16-bit, so we can do math without it overflowing.
        Func input_16("input_16");
        input_16(x, y, c) = cast<uint16_t>(input(x, y, c));

        // Blur it horizontally:
        Func blur_x("blur_x");
        blur_x(x, y, c) = (input_16(x - 1, y, c) +
                           2 * input_16(x, y, c) +
                           input_16(x + 1, y, c)) / 4;

        // Blur it vertically:
        Func blur_y("blur_y");
        blur_y(x, y, c) = (blur_x(x, y - 1, c) +
                           2 * blur_x(x, y, c) +
                           blur_x(x, y + 1, c)) / 4;

        // Convert back to 8-bit.
        Func output("output");
        output(x, y, c) = cast<uint8_t>(blur_y(x, y, c));

        Halide::Pipeline pipe(output);

        Serializer serializer;
        serializer.serialize(pipe, "test.hlb");
    }


    return 0;
}