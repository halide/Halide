#include <FImage.h>

using namespace FImage;

int main(int argc, char **argv) {

    int W = 100, H = 100;

    // Compute a random image and its true histogram
    int reference_hist[256];
    for (int i = 0; i < 256; i++) reference_hist[i] = 0;

    Image<uint8_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
            reference_hist[in(x, y)] += 1;
        }
    }

    Func hist("hist");

    RVar x, y;
    hist(in(x, y))++;
    
    if (use_gpu()) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        Var i = hist.arg(0);
        hist.split(i, bidx, tidx, 128);
        hist.parallel(bidx);
        hist.parallel(tidx);
    }

    Image<int32_t> h = hist.realize(256);

    for (int i = 0; i < 256; i++) {
        if (h(i) != reference_hist[i]) {
            printf("Error: bucket %d is %d instead of %d\n", i, h(i), reference_hist[i]);
            return -1;
        }
    }

    printf("Success!\n");

    return 0;

}
