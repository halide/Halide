#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    int W = 64*3, H = 64*3;

    Image<uint16_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
        }
    }


    Var x("x"), y("y");

    Image<uint16_t> tent(3, 3);
    tent(0, 0) = 1;
    tent(0, 1) = 2;
    tent(0, 2) = 1;
    tent(1, 0) = 2;
    tent(1, 1) = 4;
    tent(1, 2) = 2;
    tent(2, 0) = 1;
    tent(2, 1) = 2;
    tent(2, 2) = 1;
    
    Func input("input");
    input(x, y) = in(clamp(x, 0, W-1), clamp(y, 0, H-1));

    Func blur("blur");
    RDom r(tent);
    blur(x, y) += tent(r.x, r.y) * input(x + r.x - 1, y + r.y - 1);

    if (use_gpu() && false) { // TODO: broken
        blur.cudaTile(x, y, 16, 16);
    } else {
        // Take this opportunity to test tiling reductions
        Var xi, yi;
        blur.tile(x, y, xi, yi, 6, 6);
        blur.update().tile(x, y, xi, yi, 4, 4);
    }

    Image<uint16_t> out = blur.realize(W, H);

    for (int y = 1; y < H-1; y++) {
        for (int x = 1; x < W-1; x++) {
            uint16_t correct = (1*in(x-1, y-1) + 2*in(x, y-1) + 1*in(x+1, y-1) + 
                                2*in(x-1, y)   + 4*in(x, y) +   2*in(x+1, y) +
                                1*in(x-1, y+1) + 2*in(x, y+1) + 1*in(x+1, y+1));
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");

    return 0;

}
