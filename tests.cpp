#include "FImage.h"

// We use Cimg for file loading and saving
#define cimg_use_jpeg
#define cimg_use_png
#define cimg_use_tiff
#include "CImg.h"
using namespace cimg_library;

FImage load(const char *fname) {
    CImg<float> input;
    input.load_png(fname);

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
                if (output(x, y, c) > 255) output(x, y, c) = 255;
                if (output(x, y, c) < 0) output(x, y, c) = 0;
            }
        }
    }
    output.save_png(fname);
}

FImage doNothing(FImage im) {
    Var x(0, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);
    FImage output(im.size[0], im.size[1], im.size[2]);
    x.vectorize(4); x.unroll(4);
    output(x, y, c) = im(x, y, c);
    return output;
}

FImage brighten(FImage im) {
    Var x(0, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);
    FImage bright(im.size[0], im.size[1], im.size[2]);
    x.vectorize(4); y.unroll(4);
    bright(x, y, c) = (im(x, y, c) + 1)/2.0f;
    return bright;
}

FImage conditionalBrighten(FImage im) {
    Var x(0, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);
    Expr brighter = im(x, y, c)*2.0f - 0.5f;
    Expr test = (im(x, y, 0) + im(x, y, 1) + im(x, y, 2)) > 1.5f;
    FImage bright(im.size[0], im.size[1], im.size[2]);
    bright(x, y, c) = select(test, brighter, im(x, y, c));
    return bright;
}

FImage gradientx(FImage im) {
    // TODO: make x.min = 1
    
    Var x(4, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);
    FImage dx(im.size[0], im.size[1], im.size[2]);
    
    x.vectorize(4);
    x.unroll(4);

    dx(0, y, c) = 0.5f;    
    dx(1, y, c) = im(1, y, c) - im(0, y, c) + 0.5f;    
    dx(2, y, c) = im(2, y, c) - im(1, y, c) + 0.5f;
    dx(3, y, c) = im(3, y, c) - im(2, y, c) + 0.5f; 
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

    Var x(16, im.size[0]-16);
    Var y(16, im.size[1]-16);
    Var c(0, im.size[2]);

    x.vectorize(4);
    y.unroll(2);

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

    for (int c = 0; c < (int)im.size[2]; c++) {
        for (int y = 16; y < (int)im.size[1]-16; y++) {            
            for (int x = 16; x < (int)im.size[0]-16; x++) {
                float blurX = 0;
                for (int i = -K/2; i <= K/2; i++) 
                    blurX += im(x+i, y, c)*g[i+K/2];
                tmp(x, y, c) = blurX;
            }
        }
    }

    for (int c = 0; c < (int)im.size[2]; c++) {
        for (int y = 16; y < (int)im.size[1]-16; y++) {            
            for (int x = 16; x < (int)im.size[0]-16; x++) {
                float blurY = 0;
                for (int i = -K/2; i <= K/2; i++) 
                    blurY += tmp(x, y+i, c)*g[i+K/2];
                output(x, y, c) = blurY;
            }
        }
    }
}

void convolve(FImage im, FImage filter, FImage &out) {
    int mx = filter.size[0]/2;
    int my = filter.size[1]/2;
    Var x(mx, im.size[0]-mx);
    Var y(my, im.size[1]-my);
    Var c(0, im.size[2]);
    Var fx(0, filter.size[0]);
    Var fy(0, filter.size[1]);

    x.vectorize(4);

    out(x, y, c) += im(x + fx - mx, y + fy - my, c) * filter(fx, fy);

}

void convolveNative(FImage im, FImage filter, FImage &out) {
    int mx = filter.size[0]/2;
    int my = filter.size[1]/2;
    for (int c = 0; c < (int)im.size[2]; c++) {
        for (int y = my; y < (int)im.size[1]-my; y++) {
            for (int x = mx; x < (int)im.size[0]-mx; x++) {
                out(x, y, c) = 0.0f;
                for (int fy = 0; fy < (int)filter.size[1]; fy++) {
                    for (int fx = 0; fx < (int)filter.size[0]; fx++) {
                        out(x, y, c) += im(x + fx - mx, y + fy - my, c) * filter(fx, fy);
                    }
                }
            }
        }
    }
}

// A recursive box filter in x then y
FImage boxFilter(FImage im, int size) {
    Var x, y;
    Var c(0, im.size[2]);
    FImage out(im.size[0], im.size[1], im.size[2]);
    FImage tmp(im.size[0], im.size[1], im.size[2]);

    // Transformation options
    y = Var(0, im.size[1]);
    y.vectorize(4);

    // blur in X with zero boundary condition

    // Ramp up
    x = Var(1, size/2);
    tmp(0, y, c) += im(x, y, c)/size;
    tmp(x, y, c) = tmp(x-1, y, c) + im(x+size/2, y, c)/size;

    // Steady-state
    x = Var(size/2, im.size[0]-size/2);
    tmp(x, y, c) = tmp(x-1, y, c) + (im(x+size/2, y, c) - im(x-size/2, y, c))/size;

    // Ramp down
    x = Var(im.size[0]-size/2, im.size[0]);
    tmp(x, y, c) = tmp(x-1, y, c) - im(x-size/2, y, c)/size;

    // blur in Y
    x = Var(0, im.size[0]);
    x.vectorize(4);

    // Ramp up
    y = Var(1, size/2);
    out(x, 0, c) += tmp(x, y, c)/size;
    out(x, y, c) = out(x, y-1, c) + tmp(x, y+size/2, c)/size;

    // Steady-state
    y = Var(size/2, im.size[1]-size/2);
    out(x, y, c) = out(x, y-1, c) + (tmp(x, y+size/2, c) - tmp(x, y-size/2, c))/size;

    // Ramp down
    y = Var(im.size[1]-size/2, im.size[1]);
    out(x, y, c) = out(x, y-1, c) - tmp(x, y-size/2, c)/size;    

    tmp.evaluate();
    out.evaluate();

    return out;
}

// TODO: this one doesn't work yet because we don't have floor.
// It uses a reduction and a scan
#if 0
FImage histEqualize(FImage im) {    
    // 256-bin Histogram
    FImage hist(256, im.size[2]);
    Var x(0, im.size[0]);
    Var y(0, im.size[1]);
    Var c(0, im.size[2]);
    hist(floor(im(x, y, c)*256), c) += 1.0f/(im.size[0]*im.size[1]);

    // Compute the cumulative distribution by scanning along the
    // histogram
    FImage cdf(hist.size[0], im.size[2]);    
    cdf(0, c) = 0; 
    x = Var(1, hist.size[0]);
    cdf(x, c) = cdf(x-1, c) + hist(x, c);

    // Equalize im using the cdf
    FImage equalized(im.size[0], im.size[1], im.size[2]);
    x = Var(0, im.size[0]);
    y = Var(0, im.size[1]);
    equalized(x, y, c) = cdf(floor(im(x, y, c)*256), c);

    return equalized;
}

FImage bilateral(FImage im, float spatialSigma, float rangeSigma) {
    // Allocate a bilateral grid
    FImage grid(ceil(im.size[0]/spatialSigma), ceil(im.size[1]/spatialSigma), ceil(1/rangeSigma), im.size[2]+1);

    Var x(0, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);

    // Trilinear splat
    Var c(0, im.size[2]);

    int cellWidth = im.size[0]/grid.size[0];
    int cellHeight = im.size[1]/grid.size[1];
    Var gx(0, grid.size[0]);
    Var gy(0, grid.size[1]);
    Var dx(0, cellWidth*2);
    Var dy(0, cellHeight*2);        

    Expr weightXY = (1 - abs(dx - cellWidth)/cellWidth)*(1 - abs(dy - cellHeight)/cellHeight);
    Expr imX = gx*cellWidth + dx, imY = gy*cellHeight + dy;    
    Expr gridZ = sum(c, im(imX, imY, c))/im.size[2];
    Expr weightZ = gridZ - floor(gridZ);

    grid(gx, gy, floor(gridZ), c) += im(imX, imY, c) * weightXY * (1-weightZ);
    grid(gx, gy, floor(gridZ)+1, c) += im(imX, imY, c) * weightXY * weightZ;

    columnInputs = (im[...];
    gridColumn = grid[x][y][..];
    reduce(columnInputs, lambda (i:float, in:GridColumn) -> GridColumn {
               update = (0 0 ... 1 ... 0) // with the 1 at index i.z
               return k + update;
               
               // or: pass in logical in and out variables, preallocated?
               
           });
    
    
    im(x, y) sends (\col -> col[i] += 1) to grid(x/10, y/10)


    /* Alternatively do a splat from each input pixel. Has more contention.
    for (int dx = 0; dx < 2; dx++) {
        for (int dy = 0; dy < 2; dy++) {
            for (int dz = 0; dz < 2; dz++) {
                Expr weight = abs(1-dx-alphaX)*abs(1-dy-alphaY)*abs(1-dz-alphaZ);
                grid(gridX+dx, gridY+dy, gridZ+dz, c) += im(x, y, c) * weight;
                grid(gridX+dx, gridY+dy, gridZ+dz, im.size[2]) += weight;
            }
        }
    }
    */

    // Blur
    FImage blurryGridX(grid.size[0], grid.size[1], grid.size[2], grid.size[3]);
    FImage blurryGridXY(grid.size[0], grid.size[1], grid.size[2], grid.size[3]);
    FImage blurryGridXYZ(grid.size[0], grid.size[1], grid.size[2], grid.size[3]);
    
    x = Var(1, grid.size[0]-1);
    y = Var(0, grid.size[1]);
    z = Var(0, grid.size[2]);
    c = Var(0, grid.size[3]);

    blurryGridX(x, y, z, c) = (grid(x-1, y, z, c) + 2*grid(x, y, z, c) + grid(x+1, y, z, c))/4;

    y = Var(1, grid.size[1]-1);
    blurryGridXY(x, y, z, c) = (blurryGridX(x, y-1, z, c) + 2*blurryGridX(x, y, z, c) + blurryGridX(x, y+1, z, c))/4;

    z = Var(1, grid.size[2]-1);
    blurryGridXYZ(x, y, z, c) = (blurryGridXY(x, y, z-1, c) + 2*blurryGridXY(x, y, z, c) + blurryGridXY(x, y, z+1, c))/4;

    // Trilinear slice
    FImage out(im.size[0], im.size[1], im.size[2]);

    Expr gridX = x / spatialSigma;
    Expr gridY = y / spatialSigma;
    Expr gridZ = sum(c, im(x, y, c))/im.size[2];    

    Expr alphaX = gridX - floor(gridX);
    Expr alphaY = gridY - floor(gridY);
    Expr alphaZ = gridZ - floor(gridZ);

    gridX -= floor(gridX);
    gridY -= floor(gridY);
    gridZ -= floor(gridZ);   

    x = Var(0, out.size[0]);
    y = Var(0, out.size[1]);
    c = Var(0, out.size[2]);

    for (int dx = 0; dx < 2; dx++) {
        for (int dy = 0; dy < 2; dy++) {
            for (int dz = 0; dz < 2; dz++) {
                Expr weight = abs(1-dx-alphaX)*abs(1-dy-alphaY)*abs(1-dz-alphaZ);
                out(x, y, c) += weight * blurryGridXYZ(gridX+dx, gridY+dy, gridZ+dz, c);
            }
        }
    }    

    return out;
}
#endif //0


/*
// Conway's game of life (A scan that should block across the scan direction)
FImage life(FImage initial, int generations) {
    FImage grid(initial.size[0], initial.size[1], generations);

    // Use the input as the first slice
    Var x(0, initial.size[0]), y(0, initial.size[1]);
    grid(x, y, 0) = initial(x, y, 0);

    // Update slice t using slice t-1
    x = Var(1, initial.size[0]-1);
    y = Var(1, initial.size[1]-1);
    Var t(1, generations);
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
    x = Var(0, initial.size[0]);
    y = Var(0, initial.size[1]);
    out(x, y, 0) = grid(x, y, generations-1);
    return out;
}
*/

int main(int argc, char **argv) {

    if (argc != 2) {
        printf("Usage: tests.exe image.png\n");
        return -1;
    }

    FImage im = load(argv[1]);

    Var x(0, im.size[0]), y(0, im.size[1]), c(0, im.size[2]);

    // Test 0: Do nothing apart from copy the input to the output
    save(doNothing(im).evaluate(), "test_identity.png");

    // Test 1: Add one to an image
    save(brighten(im).evaluate(), "test_bright.png");

    // Test 1.5: Conditionally add one to an image
    save(conditionalBrighten(im).evaluate(), "test_cond_brighten.png");

    // Test 2: Compute horizontal derivative
    save(gradientx(im).evaluate(), "test_dx.png");

    // Test 3: Convolution
    // Make a nice sharpening filter
    FImage filter(17, 17);
    float sum = 0;
    for (int fy = 0; fy < 17; fy++) {
        for (int fx = 0; fx < 17; fx++) {
            float dx = (fx - 8)*(fx - 8);
            float dy = (fy - 8)*(fy - 8);
            filter(fx, fy) = -exp(-(dx+dy)/16);
            sum -= filter(fx, fy);
        }
    }
    for (int fy = 0; fy < 17; fy++) {
        for (int fx = 0; fx < 17; fx++) {
            filter(fx, fy) /= sum;
        }
    }        
    // Put a spike in the center
    filter(8, 8) += 2;

    FImage out(im.size[0], im.size[1], im.size[2]);
    convolve(im, filter, out);
    int t0, t1, t2, t3;
    out.evaluate(&t0);
    save(out, "test_sharp.png");
    t1 = timeGetTime();
    convolveNative(im, filter, out);
    t1 = timeGetTime() - t1;
    save(out, "test_sharpNative.png");
    printf("Sharpening: %d vs %d (speedup = %f)\n", t0, t1, (float)t1/t0);

    // Test 4: Recursive box filter
    save(boxFilter(im, 16), "test_box_filter.png");

    // Test 5: Separable Gaussian blur with timing
    FImage tmp(im.size[0], im.size[1], im.size[2]);
    FImage blurry(im.size[0], im.size[1], im.size[2]);
    const int K = 7;
    blur(im, K, tmp, blurry);    
    tmp.evaluate(&t0);
    blurry.evaluate(&t1);
    save(blurry, "test_blurry.png");

    // Do it in native C++ for comparison
    t2 = timeGetTime();
    blurNative(im, K, tmp, blurry);
    t3 = timeGetTime();
    save(blurry, "test_blurry_native.png");

    printf("FImage: %d %d ms\n", t0, t1);
    printf("Native: %d ms\n", t3-t2);

    // clock speed in cycles per millisecond
    const double clock = 3068000.0;
    long long pixels = (im.size[0]-32)*(im.size[1]-32);
    long long multiplies = pixels*im.size[2]*K;
    double f0_mpc = multiplies / (t0*clock);
    double f1_mpc = multiplies / (t1*clock);
    double n_mpc = 2*multiplies / ((t3-t2)*clock);

    double f0_cpp = (t0*clock)/pixels;
    double f1_cpp = (t1*clock)/pixels;
    double n_cpp = 0.5*((t3-t2)*clock)/pixels;

    printf("FImage: %f multiplies per cycle\n", f0_mpc);
    printf("FImage: %f multiplies per cycle\n", f1_mpc);
    printf("Native: %f multiplies per cycle\n", n_mpc);

    printf("FImage: %f cycles per pixel\n", f0_cpp);
    printf("FImage: %f cycles per pixel\n", f1_cpp);
    printf("Native: %f cycles per pixel\n", n_cpp);

    return 0;
}
