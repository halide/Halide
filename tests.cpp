#include "FImage.h"
#include "ImageStack.h"

FImage load(const char *fname) {
    ImageStack::Image input = ImageStack::Load::apply(fname);
    FImage im(input.width, input.height, input.channels);

    float *imPtr = im.data;
    for (int y = 0; y < input.height; y++) {
        for (int x = 0; x < input.width; x++) {
            for (int c = 0; c < input.channels; c++) {
                *imPtr++ = input(x, y)[c];
            }
        }
    }

    return im;
}

void save(const FImage &im, const char *fname) {
    ImageStack::Image output(im.width, im.height, 1, im.channels);
    float *imPtr = im.data;
    for (int y = 0; y < output.height; y++) {
        for (int x = 0; x < output.width; x++) {
            for (int c = 0; c < output.channels; c++) {
                output(x, y)[c] = *imPtr++;
            }
        }
    }
    ImageStack::Save::apply(output, fname);
}

int main(int argc, char **argv) {

    FImage im = load(argv[1]);

    Var x(0, im.width), y(0, im.height), c(0, im.channels);

    // Add one to an image
    FImage bright(im.width, im.height, im.channels);
    bright(x, y, c) = im(x, y, c) + 1;
    bright.evaluate();

    save(bright, "bright.tmp");

    // Compute horizontal derivative
    FImage dx(im.width, im.height, im.channels);
    x = Var(4, im.width);
    //dx(0, y, c) = im(0, y, c);    
    dx(x, y, c) = im(x, y, c) - im(x-1, y, c);

    dx.debug();
    dx.evaluate();
    save(dx, "dx.tmp");

    // Separable 11x11 Gaussian
    float g[11];
    float sum = 0;
    for (int i = 0; i < 11; i++) {
        g[i] = expf(-(i-5)*(i-5)/5.0);
        sum += g[i];
    }
    for (int i = 0; i < 11; i++) {
        g[i] /= sum;
    }

    FImage blurry(im.width, im.height, im.channels);
    FImage tmp(im.width, im.height, im.channels);

    float t0 = ImageStack::currentTime();

    x = Var(8, im.width-8);
    y = Var(8, im.height-8);

    Expr blurX = 0;
    for (int i = -5; i <= 5; i++) 
        blurX += im(x+i, y, c)*g[i+5];
    tmp(x, y, c) = blurX;

    Expr blurY = 0;
    for (int i = -5; i <= 5; i++) 
        blurY += tmp(x, y+i, c)*g[i+5];
    blurry(x, y, c) = blurY;

    tmp.evaluate();
    blurry.evaluate();

    float t1 = ImageStack::currentTime();

    save(blurry, "blurry.tmp");

    float t2 = ImageStack::currentTime();

    for (int yi = 8; yi < im.height-8; yi++) {
        for (int xi = 8; xi < im.width-8; xi++) {
            for (int ci = 0; ci < im.channels; ci++) {
                float blurX = 0.0f;
                for (int i = -5; i <= 5; i++) {
                    blurX += im(xi+i, yi, ci)*g[i+5];
                }                
                tmp(xi, yi, ci) = blurX;
            }            
        }
    }

    for (int yi = 8; yi < im.height-8; yi++) {
        for (int xi = 8; xi < im.width-8; xi++) {
            for (int ci = 0; ci < im.channels; ci++) {
                float blurY = 0.0f;
                for (int i = -5; i <= 5; i++) {
                    blurY += tmp(xi, yi+i, ci)*g[i+5];
                }                
                blurry(xi, yi, ci) = blurY;
            }            
        }
    }

    float t3 = ImageStack::currentTime();

    printf("FImage: %f\nConventional: %f\n", t1-t0, t3-t2);

    save(blurry, "blurry2.tmp");


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
