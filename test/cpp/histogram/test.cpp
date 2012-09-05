#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    int W = 128, H = 128;

    // Compute a random image and its true histogram
    int reference_hist[256];
    for (int i = 0; i < 256; i++) {
        reference_hist[i] = 0;
    }

    Image<float> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = float(rand() & 0x000000ff);
            reference_hist[uint8_t(in(x, y))] += 1;
        }
    }

    Func hist("hist");

    RDom r(in);
    hist(clamp(cast<int>(in(r.x, r.y)), 0, 255))++;

    if (use_gpu()) {
	hist.cudaTile(hist.arg(0), 64);
	hist.update().cudaTile(r.x, r.y, 16, 16);
    } else {
    
        // Grab a handle to the update step of a reduction for scheduling
        // using the "update()" method.
        Var xi, yi;
        hist.update().tile(r.x, r.y, xi, yi, 32, 32);
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
