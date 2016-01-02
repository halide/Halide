#include <stdio.h>
#include "Halide.h"

using namespace Halide;

Expr u8(Expr a) {
    return cast<uint8_t>(a);
}

int main(int argc, char **argv) {

    Image<uint8_t> input(64, 64);

    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            input(x, y) = y*64 + x;
        }
    }

    Func f;
    Var x, y;
    f(x, y) = select(((input(x, y) > 10) && (input(x, y) < 20)) ||
                     ((input(x, y) > 40) && (!(input(x, y) > 50))),
                     u8(255), u8(0));

    Image<uint8_t> output = f.realize(64, 64);

    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            bool cond = ((input(x, y) > 10) && (input(x, y) < 20)) ||
                ((input(x, y) > 40) && (!(input(x, y) > 50)));
            uint8_t correct = cond ? 255 : 0;
            if (correct != output(x, y)) {
                printf("output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;

}
