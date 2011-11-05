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
            reference_hist[input(x, y)] += 1;
        }
    }

    Var x("x"), y("y"), i("i");
    Func hist("hist");

    Func in("in");
    in(x, y) = input(x, y);

    hist(i) = 0;
    hist(input(x, y)) = hist(in(x, y)) + 1;

    hist.range(y, 0, H);
    hist.range(x, 0, W);

    //hist.trace();

    //Func cdf("cdf");
    //cdf(i) = Select(i > 0, cdf(i-1) + hist(i), hist(0));

    Var xo, xi;
    hist.split(x, xo, xi, 4);
    hist.unroll(xi);

    Image<int32_t> h = hist.realize(256);

    /*
    Func f;
    f(x, y) = hist(input(x, y));

    Image<int32_t> out = f.realize(W, H);
    */

    for (int i = 0; i < 256; i++) {
        if (h(i) != reference_hist[i]) {
            printf("Error: bucket %d is %d instead of %d\n", i, h(i), reference_hist[i]);
            return -1;
        }
    }

    printf("Success!\n");

    return 0;

}
