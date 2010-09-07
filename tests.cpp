#include "ImageStack.h"

int main(int argc, char **argv) {

    FImage im;

    im = Load::apply(argv[1]);
    Range x(0, im.width), y(0, im.height), c(0, im.channels);

    // Add one to an image
    FImage bright(im.width, im.height, im.channels);
    bright(x, y, c) = im(x, y, c) + 1;

    // Compute horizontal derivative
    FImage dx(im.width, im.height, im.channels);
    x = Range(1, im.width);
    dx(0, y, c) = im(0, y, c);    
    dx(x, y, c) = im(x, y, c) - im(x-1, y, c);

    // Separable 5x5 Gaussian
    float g[] = {0.135, 0.368, 1.0};
    FImage blurry(im.width, im.height, im.channels);
    FImage tmp(im.width, im.height, im.channels);

    x = Range(2, im.width-2);
    tmp(x, y, c) = (g[0]*im(x-2, y, c) +
                    g[1]*im(x-1, y, c) +
                    g[2]*im(x, y, c) + 
                    g[1]*im(x+1, y, c) + 
                    g[0]*im(x+2, y, c));
    
    y = Range(2, im.height-2);
    blurry(x, y, c) = (g[0]*im(x, y-2, c) + 
                       g[1]*im(x, y-1, c) + 
                       g[2]*im(x, y, c) + 
                       g[1]*im(x, y+1, c) + 
                       g[0]*im(x, y+2, c));
    
    // 256-bin Histogram
    FImage hist(256, 1, 3);
    x = Range(0, im.width);
    y = Range(0, im.height);
    hist(floor(im(x, y, c)*256), 0, c) += 1.0f/(im.width*im.height);

    // Compute the cumulative distribution by scanning along the
    // histogram
    FImage cdf(256, 1, 3);    
    cdf(0, 0, c) = 0;
    x = Range(1, 256);
    cdf(x, 0, c) = cdf(x-1, 0, c) + hist(x, 0, c);

    // Equalize im using the cdf
    FImage equalized(im.width, im.height, im.channels);
    x = Range(0, im.width);
    y = Range(0, im.height);
    equalized(x, y, c) = cdf(floor(im(x, y, c)*256), 1, c);

    // Convolve an image by a filter
    FImage filter = Load::apply("...");
    FImage filtered(im.width-filter.width+1, im.height-filter.height+1, im.channels);

    Range j(0, filter.width), k(0, filter.height);
    x = Range(0, filtered.width); 
    y = Range(0, filtered.height);    
    filtered(x, y, c) = sum(j, k, filter(j, k, 0) * im(x+j, y+k, c));

}

/*
Range + int = Range
f(Range) = Expr 

Gathers look like this:
Image(Range, Range, Range) = Expr
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
Image(Range, Range, Range) = f(Image(Range, Range, Range))
Where the two ranges don't match. The system will try to figure out the right ordering. If it can't it will complain. Boundary case definitions must also exist
Good: 

im(0, y, c) = 0;
im(x, 0, c) = 0;
im(x, y, c) = im(x-1, y, c) + im(1, y-1, c) + 1;

Bad (no possible ordering):

im(x, y, c) = im(x-1, y, c) + im(x+1, y, c);

The system will error on any other structure
*/
