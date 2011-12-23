#include <FImage.h>

using namespace FImage;

int main(int argc, char **argv) {

    int W = 100, H = 100;

    Image<uint16_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
        }
    }


    Var x("x"), y("y");

    Image<uint16_t> tent(3);
    tent(0) = 1;
    tent(1) = 2;
    tent(2) = 1;
    
    Func input("input");
    input(x, y) = in(Clamp(x, 0, W), Clamp(y, 0, H));

    Func kernel("kernel");
    kernel(x, y) = tent(x+1)*tent(y+1);

    Func blur("blur");
    RVar i(-1, 3, "i"), j(-1, 3, "j");
    blur(x, y) = Sum(kernel(i, j) * input(x + i, y + j));

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
