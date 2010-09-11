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

int main(int argc, char **argv) {

    FImage im(0, 0, 0);
    if (argc == 1)
        im = load("dog_big.jpg");
    else 
        im = load(argv[1]);

    Var x(0, im.width), y(0, im.height), c(0, im.channels);

    // Add one to an image
    FImage bright(im.width, im.height, im.channels);
    bright(x, y, c) = (im(x, y, c) + 1)/2.0f;
    bright.evaluate();

    save(bright, "bright.jpg");

    // Compute horizontal derivative
    FImage dx(im.width, im.height, im.channels);
    x = Var(4, im.width);
    //dx(0, y, c) = im(0, y, c);    
    dx(x, y, c) = (im(x, y, c) - im(x-1, y, c))+0.5f;

    dx.debug();
    dx.evaluate();
    save(dx, "dx.jpg");

    // Separable KxK Gaussian
    #define K 19
    float g[K];
    float sum = 0;
    for (int i = 0; i < K; i++) {
        g[i] = expf(-(i-K/2)*(i-K/2)/(0.125f*K*K));
        sum += g[i];
    }
    for (int i = 0; i < K; i++) {
        g[i] /= sum;
    }

    FImage blurry(im.width, im.height, im.channels);
    FImage tmp(im.width, im.height, im.channels);

    x = Var(16, im.width-16);
    y = Var(16, im.height-16);

    Expr blurX = 0;
    for (int i = -K/2; i <= K/2; i++) 
        blurX += im(x+i, y, c)*g[i+K/2];
    tmp(x, y, c) = blurX;

    Expr blurY = 0;
    for (int i = -K/2; i <= K/2; i++) 
        blurY += tmp(x, y+i, c)*g[i+K/2];
    blurry(x, y, c) = blurY;

    int t0 = timeGetTime();   

    tmp.evaluate();

    int t1 = timeGetTime();

    blurry.evaluate();

    int t2 = timeGetTime();

    save(blurry, "blurry.jpg");

    int t3 = timeGetTime();

    for (int yi = 16; yi < im.height-16; yi++) {
        for (int xi = 16; xi < im.width-16; xi++) {
            for (int ci = 0; ci < im.channels; ci++) {
                float blurX = 0.0f;
                for (int i = -K/2; i <= K/2; i++) {
                    blurX += im(xi+i, yi, ci)*g[i+K/2];
                }                
                tmp(xi, yi, ci) = blurX;
            }            
        }
    }

    int t4 = timeGetTime();

    for (int yi = 16; yi < im.height-16; yi++) {
        for (int xi = 16; xi < im.width-16; xi++) {
            for (int ci = 0; ci < im.channels; ci++) {
                float blurY = 0.0f;
                for (int i = -K/2; i <= K/2; i++) {
                    blurY += tmp(xi, yi+i, ci)*g[i+K/2];
                }                
                blurry(xi, yi, ci) = blurY;
            }            
        }
    }

    int t5 = timeGetTime();

    printf("FImage: %d %d\n", t1-t0, t2-t1);
    printf("Conventional: %d %d\n", t4-t3, t5-t4);

    // clock speed in cycles per millisecond
    const float clock = 2660000.0f;
    int multiplies = (im.width-32)*(im.height-32)*im.channels*K;
    float f_mpc1 = multiplies / ((t1-t0)*clock);
    float f_mpc2 = multiplies / ((t2-t1)*clock);
    float c_mpc1 = multiplies / ((t4-t3)*clock);
    float c_mpc2 = multiplies / ((t5-t4)*clock);

    printf("FImage: %f %f multiplies per clock\n", f_mpc1, f_mpc2);
    printf("Conventional: %f %f multiplies per clock\n", c_mpc1, c_mpc2);

    save(blurry, "blurry2.jpg");


    return 0;

    /*
    // 256-bin Histogram
    FImage hist(256, 1, 3);
    x = Var(0, im.width);
    y = Var(0, im.height);
    hist(floor(im(x, y, c)*256), 0, c) += 1.0f/(im.width*im.height);

    // Compute the cumulative distribution by scanning along the
    // histogram
    FImage cdf(256, 1, 3);    
    cdf(0, 0, c) = 0;
    x = Var(1, 256);
    cdf(x, 0, c) = cdf(x-1, 0, c) + hist(x, 0, c);

    // Equalize im using the cdf
    FImage equalized(im.width, im.height, im.channels);
    x = Var(0, im.width);
    y = Var(0, im.height);
    equalized(x, y, c) = cdf(floor(im(x, y, c)*256), 1, c);

    // Convolve an image by a filter
    FImage filter = Load::apply("...");
    FImage filtered(im.width-filter.width+1, im.height-filter.height+1, im.channels);

    Var j(0, filter.width), k(0, filter.height);
    x = Var(0, filtered.width); 
    y = Var(0, filtered.height);    
    filtered(x, y, c) = sum(j, k, filter(j, k, 0) * im(x+j, y+k, c));
    */
}

/*
f(Var) = Expr 

Gathers look like this:
Image(Var, Var, Var) = Expr
where the RHS does not reference the same Image
Gathers are detected and optimized by:
  unrolling across the inner arg
  vectorizing across the middle arg
  parallelizing across the outer arg

Reductions look like this:
Image(Expr, Expr, Expr) = f(Image(Expr, Expr, Expr))
Where both instances of the image have the same arguments
reductions can be optimized similarly to gathers, but parallelization is slightly trickier

Scans look like this
Image(Var, Var, Var) = f(Image(Var, Var, Var))
Where the two ranges don't match. The system will try to figure out the right ordering. If it can't it will complain. Boundary case definitions must also exist
Good: 

im(0, y, c) = 0;
im(x, 0, c) = 0;
im(x, y, c) = im(x-1, y, c) + im(1, y-1, c) + 1;

Bad (no possible ordering):

im(x, y, c) = im(x-1, y, c) + im(x+1, y, c);

The system will error on any other structure
*/
