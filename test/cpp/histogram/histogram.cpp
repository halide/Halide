#include <FImage.h>

using namespace FImage;

int main(int argc, char **argv) {

    int W = 100, H = 100;

    // Compute a random image and its true histogram
    int reference_hist[256];
    for (int i = 0; i < 256; i++) reference_hist[i] = 0;

    Image<uint8_t> input(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = rand() & 0xff;
            hist[input(x, y)] += 1;
        }
    }

    Var x, y, i;
    Func hist;

    hist(i) = 0;
    hist(input(x, y)) = hist(input(x, y)) + 1;

    hist.range(x, 0, W);
    hist.range(y, 0, H);

    Var xo, xi;
    hist.split(x, xo, xi, 4);
    hist.unroll(xi);
    
    Image<int32_t> h = hist.realize(256);

    Func f;
    f(x, y) = hist(input(x, y));

    Image<int32_t> out = f.realize(W, H);

    for (int i = 0; i < 256; i++) {
        if (h[i] != reference_hist[i]) return -1;
    }

    return 0;

}
