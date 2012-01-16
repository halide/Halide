#include <FImage.h>

using namespace FImage;

int main(int argc, char **argv) {

    int W = 100, H = 100;

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

    RVar x, y;
    hist(Clamp(Cast<int>(in(x, y)), 0, 255))++;

    if (use_gpu()) {
        Var tx("threadidx"),
            bx("blockidx"),
            ty("threadidy"),
            by("blockidy");
        
        hist.split(hist.arg(0), bx, tx, 64).parallel(bx).parallel(tx);
        
        Func& update = hist.update();
        update.split(x, bx, tx, 10).split(y, by, ty, 10).transpose(bx, ty)
            .parallel(bx).parallel(by).parallel(tx).parallel(ty);
    }
    
    // hist.compileJIT();
    // return 0;
    
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
