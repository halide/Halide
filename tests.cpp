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

    float *imPtr = im.data;
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            for (int c = 0; c < input.spectrum(); c++) {
                *imPtr++ = input(x, y, c)/256.0f;
            }
        }
    }

    return im;
}

void save(const FImage &im, const char *fname) {
    CImg<float> output(im.width, im.height, 1, im.channels);
    float *imPtr = im.data;
    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            for (int c = 0; c < output.spectrum(); c++) {
                output(x, y, c) = 256*(*imPtr++);
            }
        }
    }
    output.save_jpeg(fname);
}

FImage brighten(FImage im) {
    Var x(0, im.width), y(0, im.height), c(0, im.channels);
    FImage bright(im.width, im.height, im.channels);
    bright(x, y, c) = (im(x, y, c) + 1)/2.0f;
    return bright;
}

FImage gradientx(FImage im) {
    // TODO: make x.min = 1, and allow the base case definition
    
    Var x(4, im.width), y(0, im.height), c(0, im.channels);
    FImage dx(im.width, im.height, im.channels);
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

    Var x(16, im.width-16);
    Var y(16, im.height-16);
    Var c(0, im.channels);

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

    for (int y = 16; y < im.height-16; y++) {            
        for (int x = 16; x < im.width-16; x++) {
            for (int c = 0; c < im.channels; c++) {
                float blurX = 0;
                for (int i = -K/2; i <= K/2; i++) 
                    blurX += im(x+i, y, c)*g[i+K/2];
                tmp(x, y, c) = blurX;
            }
        }
    }

    for (int y = 16; y < im.height-16; y++) {            
        for (int x = 16; x < im.width-16; x++) {
            for (int c = 0; c < im.channels; c++) {
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
    FImage hist(hist.width, 1, im.channels);
    Var x(0, im.width);
    Var y(0, im.height);
    hist(floor(im(x, y, c)*256), 0, c) += 1.0f/(im.width*im.height);

    // Compute the cumulative distribution by scanning along the
    // histogram
    FImage cdf(hist.width, 1, im.channels);    
    cdf(0, 0, c) = 0;
    x = Var(1, hist.width);
    cdf(x, 0, c) = cdf(x-1, 0, c) + hist(x, 0, c);

    // Equalize im using the cdf
    FImage equalized(im.width, im.height, im.channels);
    x = Var(0, im.width);
    y = Var(0, im.height);
    equalized(x, y, c) = cdf(floor(im(x, y, c)*256), 1, c);

    return equalized();
}
*/

int main(int argc, char **argv) {

    FImage im(0, 0, 0);
    if (argc == 1)
        im = load("dog_big.jpg");
    else 
        im = load(argv[1]);

    Var x(0, im.width), y(0, im.height), c(0, im.channels);

    // Test 1: Add one to an image
    save(brighten(im).evaluate(), "bright.jpg");

    // Test 2: Compute horizontal derivative
    save(gradientx(im).evaluate(), "dx.jpg");

    // Test 3: Separable Gaussian blur
    FImage tmp(im.width, im.height, im.channels);
    FImage blurry(im.width, im.height, im.channels);
    const int K = 19;
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
    long long pixels = (im.width-32)*(im.height-32)*2;
    long long multiplies = pixels*im.channels*K;
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
