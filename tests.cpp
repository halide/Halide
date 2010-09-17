#include "FImage.h"

// We use Cimg for file loading and saving
#define cimg_use_jpeg
#define cimg_use_png
#define cimg_use_tiff
#include "CImg.h"
using namespace cimg_library;

FImage load(const char *fname) {
    CImg<float> input;
    input.load_jpeg(fname);

    FImage im(input.width(), input.height(), input.spectrum());

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            for (int c = 0; c < input.spectrum(); c++) {
                im(x, y, c) = input(x, y, c)/256.0f;
            }
        }
    }

    return im;
}

void save(const FImage &im, const char *fname) {
    CImg<float> output(im.size[0], im.size[1], 1, im.size[2]);
    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            for (int c = 0; c < output.spectrum(); c++) {
                output(x, y, c) = 256*im(x, y, c);
            }
        }
    }
    output.save_jpeg(fname);
}

FImage brighten(FImage im) {
    Range x(0, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);
    FImage bright(im.size[0], im.size[1], im.size[2]);
    bright(x, y, c) = (im(x, y, c) + 1)/2.0f;
    return bright;
}

FImage gradientx(FImage im) {
    // TODO: make x.min = 1, and allow the base case definition
    
    Range x(4, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);
    FImage dx(im.size[0], im.size[1], im.size[2]);
    //dx(0, y, c) = im(0, y, c);    
    dx(x, y, c) = (im(x, y, c) - im(x-1, y, c))+0.5f;
    return dx;
}

void blur(FImage im, const int K, FImage &tmp, FImage &output) {
    // create a gaussian kernel
    vector<float> g(K);
    float sum = 0;
    for (int i = 0; i < K; i++) {
        g[i] = expf(-(i-K/2)*(i-K/2)/(0.125f*K*K));
        sum += g[i];
    }
    for (int i = 0; i < K; i++) {
        g[i] /= sum;
    }

    Range x(16, im.size[0]-16);
    Range y(16, im.size[1]-16);
    Range c(0, im.size[2]);

    Expr blurX = 0;
    for (int i = -K/2; i <= K/2; i++) 
        blurX += im(x+i, y, c)*g[i+K/2];
    tmp(x, y, c) = blurX;

    Expr blurY = 0;
    for (int i = -K/2; i <= K/2; i++) 
        blurY += tmp(x, y+i, c)*g[i+K/2];
    output(x, y, c) = blurY;
}

void blurNative(FImage im, const int K, FImage &tmp, FImage &output) {
    // create a gaussian kernel
    vector<float> g(K);
    float sum = 0;
    for (int i = 0; i < K; i++) {
        g[i] = expf(-(i-K/2)*(i-K/2)/(0.125f*K*K));
        sum += g[i];
    }
    for (int i = 0; i < K; i++) {
        g[i] /= sum;
    }

    for (int c = 0; c < im.size[2]; c++) {
        for (int y = 16; y < im.size[1]-16; y++) {            
            for (int x = 16; x < im.size[0]-16; x++) {
                float blurX = 0;
                for (int i = -K/2; i <= K/2; i++) 
                    blurX += im(x+i, y, c)*g[i+K/2];
                tmp(x, y, c) = blurX;
            }
        }
    }

    for (int c = 0; c < im.size[2]; c++) {
        for (int y = 16; y < im.size[1]-16; y++) {            
            for (int x = 16; x < im.size[0]-16; x++) {
                float blurY = 0;
                for (int i = -K/2; i <= K/2; i++) 
                    blurY += tmp(x, y+i, c)*g[i+K/2];
                output(x, y, c) = blurY;
            }
        }
    }
}

// TODO: this one doesn't work at all. It uses a reduction and a scan
/*
FImage histEqualize(FImage im) {    
    // 256-bin Histogram
    FImage hist(hist.size[0], 1, im.size[2]);
    Range x(0, im.size[0]);
    Range y(0, im.size[1]);
    hist(floor(im(x, y, c)*256), 0, c) += 1.0f/(im.size[0]*im.size[1]);

    // Compute the cumulative distribution by scanning along the
    // histogram
    FImage cdf(hist.size[0], 1, im.size[2]);    
    cdf(0, 0, c) = 0;
    x = Range(1, hist.size[0]);
    cdf(x, 0, c) = cdf(x-1, 0, c) + hist(x, 0, c);

    // Equalize im using the cdf
    FImage equalized(im.size[0], im.size[1], im.size[2]);
    x = Range(0, im.size[0]);
    y = Range(0, im.size[1]);
    equalized(x, y, c) = cdf(floor(im(x, y, c)*256), 1, c);

    return equalized();
}
*/


/*
// Conway's game of life (A scan that should block across the scan direction)
FImage life(FImage initial, int generations) {
    FImage grid(initial.size[0], initial.size[1], generations);

    // Use the input as the first slice
    Range x(0, initial.size[0]), y(0, initial.size[1]);
    grid(x, y, 0) = initial(x, y, 0);

    // Update slice t using slice t-1
    x = Range(1, initial.size[0]-1);
    y = Range(1, initial.size[1]-1);
    Range t(1, generations);
    Expr live = grid(x, y, t-1);
    Expr sum = (grid(x-1, y, t-1) +
                grid(x, y-1, t-1) + 
                grid(x+1, y, t-1) + 
                grid(x, y+1, t-1) + 
                grid(x-1, y-1, t-1) + 
                grid(x+1, y-1, t-1) + 
                grid(x-1, y+1, t-1) + 
                grid(x+1, y+1, t-1));

    grid(x, y, t) = (sum == 3) || (live && (sum == 2));

    // Grab the last slice as the output
    FImage out(initial.size[0], initial.size[1], 1);
    x = Range(0, initial.size[0]);
    y = Range(0, initial.size[1]);
    out(x, y, 0) = grid(x, y, generations-1);
    return out;
}
*/

int main(int argc, char **argv) {

    if (argc != 2) {
        printf("Usage: tests.exe image.jpg\n");
        return -1;
    }

    FImage im = load(argv[1]);

    Range x(0, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);

    // Test 1: Add one to an image
    save(brighten(im).evaluate(), "bright.jpg");

    // Test 2: Compute horizontal derivative
    save(gradientx(im).evaluate(), "dx.jpg");

    // Test 3: Separable Gaussian blur with timing
    FImage tmp(im.size[0], im.size[1], im.size[2]);
    FImage blurry(im.size[0], im.size[1], im.size[2]);
    const int K = 9;
    int t0 = timeGetTime();
    blur(im, K, tmp, blurry);
    tmp.evaluate();
    blurry.evaluate();
    int t1 = timeGetTime();
    save(blurry, "blurry.jpg");

    // Do it in native C++ for comparison
    int t2 = timeGetTime();
    blurNative(im, K, tmp, blurry);
    int t3 = timeGetTime();
    save(blurry, "blurry_native.jpg");

    printf("FImage: %d ms\n", t1-t0);
    printf("Native: %d ms\n", t3-t2);

    // clock speed in cycles per millisecond
    const double clock = 2130000.0;
    long long pixels = (im.size[0]-32)*(im.size[1]-32)*2;
    long long multiplies = pixels*im.size[2]*K;
    double f_mpc = multiplies / ((t1-t0)*clock);
    double n_mpc = multiplies / ((t3-t2)*clock);

    double f_ppc = ((t1-t0)*clock)/pixels;
    double n_ppc = ((t3-t2)*clock)/pixels;

    printf("FImage: %f multiplies per cycle\n", f_mpc);
    printf("Native: %f multiplies per cycle\n", n_mpc);

    printf("FImage: %f cycles per pixel\n", f_ppc);
    printf("Native: %f cycles per pixel\n", n_ppc);

    return 0;
}
