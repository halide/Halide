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

    hist(input(x, y)) += 1;
    
    Func f;
    f(x, y) = hist(input(x, y));

    Image<int32_t> out = f.realize(W, H);

    for (int i = 0; i < 256; i++) {
        if (h[i] != reference_hist[i]) return -1;
    }

    return 0;

}
