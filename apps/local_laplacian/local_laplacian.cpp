#include <FImage.h>
#include "../png.h"

using namespace FImage;

// We don't have exp in the language yet, a rough approximation to a Gaussian-like function
Expr gaussian(Expr x) {
    return 1.0f/(x*x+1.0f);
}

// Remap x using y as the central point, an amplitude of alpha, and a std.dev of sigma
Expr remap(Expr x, Expr y, Expr alpha, Expr beta, Expr sigma) {
    return y + (x - y) * (beta + alpha * gaussian((x - y)/sigma));
}

Func downsample(Func f) {
    Var x, y;
    Func downx, downy;
    
    downx(x, y) = (f(2*x-1, y) + 3.0f * (f(2*x, y) + f(2*x+1, y)) + f(2*x+2, y)) / 8.0f;    
    downy(x, y) = (downx(x, 2*y-1) + 3.0f * (downx(x, 2*y) + downx(x, 2*y+1)) + downx(x, 2*y+2)) / 8.0f;

    return downy;
}

Func upsample(Func f) {
    Var x, y;
    Func upx, upy;

    upx(x, y) = 0.25f * f((x/2) - 1 + 2*(x % 2), y) + 0.75f * f(x/2, y);
    upy(x, y) = 0.25f * upx(x, (y/2) - 1 + 2*(y % 2)) + 0.75f * upx(x, y/2);

    return upy;
}

int main(int argc, char **argv) {

    // Number of pyramid levels 
    int J = 8;

    // number of intensity levels
    Uniform<int> levels("levels");
    // Parameters controlling the filter
    Uniform<float> alpha("alpha"), beta("beta");
    // Takes a 16-bit input
    UniformImage input(UInt(16), 3);

    assert(J <= 12);

    // loop variables
    Var x, y, c, k;

    // Convert to floating point
    Func floating("floating");
    floating(x, y, c) = Cast<float>(input(x, y, c)) / 65535.0f;
    
    // Set a boundary condition
    Func clamped("clamped");
    clamped(x, y, c) = floating(Clamp(x, 0, input.width()-1), Clamp(y, 0, input.height()-1), c);
    
    // Get the luminance channel
    Func gray("gray");
    gray(x, y) = 0.299f * clamped(x, y, 0) + 0.587f * clamped(x, y, 1) + 0.114f * clamped(x, y, 2);
    
    // Make the processed Gaussian pyramid
    Func gPyramid[] = {"gp0", "gp1", "gp2", "gp3", "gp4", "gp5", "gp6", "gp7", "gp8", "gp9", "gp10", "gp11"};
    gPyramid[0](x, y, k) = remap(gray(x, y), Cast<float>(k) / (levels-1), alpha, beta, 1.0f / (levels-1));
    for (int j = 1; j < J; j++)
        gPyramid[j](x, y, k) = downsample(gPyramid[j-1])(x, y, k);
    
    // Get its laplacian pyramid
    Func lPyramid[] = {"lp0", "lp1", "lp2", "lp3", "lp4", "lp5", "lp6", "lp7", "lp8", "lp9", "lp10", "lp11"};
    lPyramid[J-1] = gPyramid[J-1];
    for (int j = J-2; j >= 0; j--) 
        lPyramid[j](x, y, k) = gPyramid[j](x, y, k) - upsample(gPyramid[j+1])(x, y, k);    

    // Make the Gaussian pyramid of the input
    Func inGPyramid[] = {"igp0", "igp1", "igp2", "igp3", "igp4", "igp5", "igp6", "igp7", "igp8", "igp9", "igp10", "igp11"};
    inGPyramid[0] = gray;
    for (int j = 1; j < J; j++)
        inGPyramid[j](x, y) = downsample(inGPyramid[j-1])(x, y);
        
    // Make the laplacian pyramid of the output
    Func outLPyramid[] = {"olp0", "olp1", "olp2", "olp3", "olp4", "olp5", "olp6", "olp7", "olp8", "olp9", "olp10", "olp11"};
    for (int j = 0; j < J; j++) {
        // Split input pyramid value into integer and floating parts
        Expr level = inGPyramid[j](x, y) * Cast<float>(levels-1);
        Expr li = Clamp(Cast<int>(level), 0, levels-2);
        Expr lf = level - Cast<float>(li);
        // Linearly interpolate between the nearest processed pyramid levels
        outLPyramid[j](x, y) = (1.0f - lf) * lPyramid[j](x, y, li) + lf * lPyramid[j](x, y, li+1);
    }
    
    // Make the Gaussian pyramid of the output
    Func outGPyramid[] = {"ogp0", "ogp1", "ogp2", "ogp3", "ogp4", "ogp5", "ogp6", "ogp7", "ogp8", "ogp9", "ogp10", "ogp11"};
    outGPyramid[J-1] = outLPyramid[J-1];
    for (int j = J-2; j >= 0; j--) 
        outGPyramid[j](x, y) = upsample(outGPyramid[j+1])(x, y) + outLPyramid[j](x, y);
    
    // Reintroduce color
    Func color;
    color(x, y, c) = outGPyramid[0](x, y) * clamped(x, y, c) / gray(x, y);
        
    Func output;
    // Convert back to 16-bit
    Var xo, yo;
    output(xo, yo, c) = Cast<uint16_t>(Clamp(color(xo, yo, c), 0.0f, 1.0f) * 65535.0f);


    // Breadth-first, with some unrolling to clean up upsampling and downsampling a little

    Var xi, yi;

    switch (atoi(argv[1])) {
    case 0:
        // breadth-first scalar: 2159 ms
        output.root();
        for (int j = 0; j < J; j++) {
            inGPyramid[j].root();
            gPyramid[j].root();
            outGPyramid[j].root();
            if (j == J-1) break;
            lPyramid[j].root();
            outLPyramid[j].root();
        }
        break;        
    case 1:
        // parallelize each stage across outermost dimension: 944 ms
        output.split(yo, yo, yi, 128).parallel(yo);
        for (int j = 0; j < J; j++) {
            inGPyramid[j].root().split(y, y, yi, 4).parallel(y);
            gPyramid[j].root().parallel(k);
            outGPyramid[j].root().split(y, y, yi, 4).parallel(y);
            if (j == J-1) break;
            lPyramid[j].root().parallel(k);
            outLPyramid[j].root().split(y, y, yi, 4).parallel(y);
        }
        break;        
    case 2:
        // Same as above, but also vectorize heavily across x: 952 ms
        output.split(yo, yo, yi, 128).parallel(yo).vectorize(xo, 4);
        for (int j = 0; j < J; j++) {
            inGPyramid[j].root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
            gPyramid[j].root().parallel(k).vectorize(x, 4);
            outGPyramid[j].root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
            if (j == J-1) break;
            lPyramid[j].root().parallel(k).vectorize(x, 4);
            outLPyramid[j].root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
        }
        break;
    case 3:
        // parallelize across yi instead of yo: Bad idea - 1747 ms
        output.split(yo, yo, yi, 8).parallel(yi);
        for (int j = 0; j < J; j++) {
            inGPyramid[j].root().split(y, y, yi, 8).parallel(yi);
            gPyramid[j].root().parallel(k);
            outGPyramid[j].root().split(y, y, yi, 8).parallel(yi);
            if (j == J-1) break;
            lPyramid[j].root().parallel(k);
            outLPyramid[j].root().split(y, y, yi, 8).parallel(yi);
        }
        break;        
    case 4:
        // Parallelize, inlining all the laplacian pyramid levels
        // (they can be computed from the gaussian pyramids on the
        // fly): 750 ms
        output.split(yo, yo, yi, 128).parallel(yo);
        for (int j = 0; j < J; j++) {
            inGPyramid[j].root().split(y, y, yi, 1).parallel(y);
            gPyramid[j].root().parallel(k);
            outGPyramid[j].root().split(y, y, yi, 1).parallel(y);
        }
        break;                
    case 5:
        // Same as above with vectorization (now that we're doing more
        // math and less memory, maybe it will matter): 646 ms
        output.split(yo, yo, yi, 128).parallel(yo).vectorize(xo, 4);
        for (int j = 0; j < J; j++) {
            inGPyramid[j].root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
            gPyramid[j].root().parallel(k).vectorize(x, 4);
            outGPyramid[j].root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
        }
        break;
    case 6:
        // Also inline every other pyramid level: Bad idea - 2109 ms
        output.split(yo, yo, yi, 128).parallel(yo).vectorize(xo, 4);
        for (int j = 0; j < J; j+=2) {
            inGPyramid[j].root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
            gPyramid[j].root().parallel(k).vectorize(x, 4);
            outGPyramid[j].root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
        }
        break;
    case 7:
        // Take care of the boundary condition earlier to avoid costly branching: 808 ms
        output.split(yo, yo, yi, 128).parallel(yo).vectorize(xo, 4);
        clamped.root().split(y, y, yi, 128).parallel(y).vectorize(x, 4);
        for (int j = 0; j < J; j++) {
            inGPyramid[j].root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
            gPyramid[j].root().parallel(k).vectorize(x, 4);
            outGPyramid[j].root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
        }
        break;
    case 8:
        // Unroll by a factor of two to try and simplify the upsampling math: not worth it - 836 ms
        output.split(yo, yo, yi, 128).parallel(yo).unroll(xo, 2).unroll(yi, 2);
        for (int j = 0; j < J; j++) {
            inGPyramid[j].root().split(y, y, yi, 4).parallel(y).unroll(x, 2).unroll(y, 2);
            gPyramid[j].root().parallel(k).unroll(x, 2).unroll(y, 2);
            outGPyramid[j].root().split(y, y, yi, 4).parallel(y).unroll(x, 2).unroll(y, 2);
        }
        break;                        
    case 9:
        // 
    default: 
        break;
    }

    output.compileToFile("local_laplacian");

    return 0;
}

