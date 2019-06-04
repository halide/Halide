#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x*x) % hist_size;

    hist(x) = 0.0f;
    hist(im(r)) += 1.0f;

    hist.compute_root();
    hist.update().atomic().parallel(r);

    Buffer<float> correct(hist_size);
    correct.fill(0.0f);
    for (int i = 0; i < img_size; i++) {
        correct((i*i) % hist_size) += 1.0f;
    }

    for (int iter = 0; iter < 100; iter++) {
        Buffer<float> out = hist.realize(hist_size);
        for (int i = 0; i < hist_size; i++) {
            if (out(i) != correct(i)) {
                printf("out(%d) = %f instead of %f\n", i, out(i), correct(i));
                return -1;
            }
        }
    }

    return 0;
}