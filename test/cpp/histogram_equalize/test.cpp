#include <FImage.h>

using namespace FImage;

int main(int argc, char **argv) {

    int W = 1000, H = 1000;

    // Compute a random 8-bit image with a very biased histogram
    Image<uint8_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            unsigned r1 = rand() & 0xffff;
            unsigned r2 = rand() & 0xffff;
            in(x, y) = ((r1*r2) >> 25) + 64;
        }
    }

    Func hist, cdf, equalized, rescaled;

    RVar rx, ry, ri(0, 255);
    Var x, y, i;

    // Compute the histogram
    hist(in(rx, ry))++;

    // Integrate it to produce a cdf
    cdf(i) = 0;
    cdf(ri) = cdf(ri-1) + hist(ri);

    // Remap the input using the cdf
    equalized(x, y) = cdf(in(x, y));

    // Scale the result back to 8-bit
    int pixels = in.width()*in.height();
    rescaled = Cast<uint8_t>((equalized*256)/pixels);

    Image<uint8_t> out = rescaled.realize(in.width(), in.height());    

    // Compute the histogram of the output
    int out_hist[16], in_hist[16];
    for (int i = 0; i < 16; i++) {
        out_hist[i] = in_hist[i] = 0;
    }
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            out_hist[out(x, y)/16]++;
            in_hist[in(x, y)/16]++;
        }
    }
    for (int i = 0; i < 16; i++) {
        printf("%d %d\n", in_hist[i], out_hist[i]);
        // There should be roughly 1000*1000/16 pixels per bucket = 62500
        int correct = (in.width()*in.height())/16;
        if (out_hist[i] < correct/2 || out_hist[i] > 2*correct) {
            printf("Expected histogram entries of ~ %d\n", correct);
            return -1;
        }
    }


    printf("Success!\n");

    return 0;

}
