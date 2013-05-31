#include <Halide.h>
using namespace Halide;

Var x, y;

// Downsample with a 1 3 3 1 filter
Func downsample(Func f) {
    Func downx, downy;
    
    downx(x, y) = (f(2*x-1, y) + 3.0f * (f(2*x, y) + f(2*x+1, y)) + f(2*x+2, y)) / 8.0f;    
    downy(x, y) = (downx(x, 2*y-1) + 3.0f * (downx(x, 2*y) + downx(x, 2*y+1)) + downx(x, 2*y+2)) / 8.0f;

    return downy;
}

// Upsample using bilinear interpolation
Func upsample(Func f) {
    Func upx, upy;
    
    upx(x, y) = 0.25f * f((x/2) - 1 + 2*(x % 2), y) + 0.75f * f(x/2, y);
    upy(x, y) = 0.25f * upx(x, (y/2) - 1 + 2*(y % 2)) + 0.75f * upx(x, y/2);

    return upy;
    
}

int main(int argc, char **argv) {

    /* THE ALGORITHM */

    // Number of pyramid levels 
    const int J = 8;

    // number of intensity levels
    Param<int> levels;
    // Parameters controlling the filter
    Param<float> alpha, beta;
    // Takes a 16-bit input
    ImageParam input(UInt(16), 3);

    // loop variables
    Var c, k;

    // Make the remapping function as a lookup table.
    Func remap;
    Expr fx = cast<float>(x) / 256.0f;
    remap(x) = alpha*fx*exp(-fx*fx/2.0f);
    
    // Convert to floating point
    Func floating;
    floating(x, y, c) = cast<float>(input(x, y, c)) / 65535.0f;
    
    // Set a boundary condition
    Func clamped;
    clamped(x, y, c) = floating(clamp(x, 0, input.width()-1), clamp(y, 0, input.height()-1), c);
    
    // Get the luminance channel
    Func gray;
    gray(x, y) = 0.299f * clamped(x, y, 0) + 0.587f * clamped(x, y, 1) + 0.114f * clamped(x, y, 2);

    // Make the processed Gaussian pyramid. 
    Func gPyramid[J];
    // Do a lookup into a lut with 256 entires per intensity level
    Expr idx = gray(x, y)*cast<float>(levels-1)*256.0f;
    idx = clamp(cast<int>(idx), 0, (levels-1)*256);
    gPyramid[0](x, y, k) = beta*gray(x, y) + remap(idx - 256*k);
    for (int j = 1; j < J; j++) {
        gPyramid[j](x, y, k) = downsample(gPyramid[j-1])(x, y, k);
    }    

    // Get its laplacian pyramid
    Func lPyramid[J];
    lPyramid[J-1](x, y, k) = gPyramid[J-1](x, y, k);
    for (int j = J-2; j >= 0; j--) {
        lPyramid[j](x, y, k) = gPyramid[j](x, y, k) - upsample(gPyramid[j+1])(x, y, k);
    }

    // Make the Gaussian pyramid of the input
    Func inGPyramid[J];
    inGPyramid[0](x, y) = gray(x, y);
    for (int j = 1; j < J; j++) {
        inGPyramid[j](x, y) = downsample(inGPyramid[j-1])(x, y);
    }        

    // Make the laplacian pyramid of the output
    Func outLPyramid[J];
    for (int j = 0; j < J; j++) {
        // Split input pyramid value into integer and floating parts
        Expr level = inGPyramid[j](x, y) * cast<float>(levels-1);
        Expr li = clamp(cast<int>(level), 0, levels-2);
        Expr lf = level - cast<float>(li);
        // Linearly interpolate between the nearest processed pyramid levels
        outLPyramid[j](x, y) = (1.0f - lf) * lPyramid[j](x, y, li) + lf * lPyramid[j](x, y, li+1);
    }
    
    // Make the Gaussian pyramid of the output
    Func outGPyramid[J];
    outGPyramid[J-1](x, y) = outLPyramid[J-1](x, y);
    for (int j = J-2; j >= 0; j--) {
        outGPyramid[j](x, y) = upsample(outGPyramid[j+1])(x, y) + outLPyramid[j](x, y);
    }    

    // Reintroduce color (Connelly: use eps to avoid scaling up noise w/ apollo3.png input)
    Func color;
    float eps = 0.01f;
    color(x, y, c) = outGPyramid[0](x, y) * (clamped(x, y, c)+eps) / (gray(x, y)+eps);
        
    Func output("local_laplacian");
    // Convert back to 16-bit
    output(x, y, c) = cast<uint16_t>(clamp(color(x, y, c), 0.0f, 1.0f) * 65535.0f);



    /* THE SCHEDULE */
    remap.compute_root();

    char *target = getenv("HL_TARGET");
    if (target && std::string(target) == "ptx") {
        // gpu schedule
        output.compute_root().cuda_tile(x, y, 32, 32);
        for (int j = 0; j < J; j++) {
            int blockw = 32, blockh = 16;
            if (j > 3) {
                blockw = 2;
                blockh = 2;
            }
            if (j > 0) inGPyramid[j].compute_root().cuda_tile(x, y, blockw, blockh);
            if (j > 0) gPyramid[j].compute_root().reorder(k, x, y).cuda_tile(x, y, blockw, blockh);
            outGPyramid[j].compute_root().cuda_tile(x, y, blockw, blockh);
        }
    } else {
        // cpu schedule
        Var yi;
        output.split(y, y, yi, 4).parallel(y).vectorize(x, 4);
        gray.compute_root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
        for (int j = 0; j < 4; j++) {
            if (j > 0) inGPyramid[j].compute_root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
            if (j > 0) gPyramid[j].compute_root().parallel(k).vectorize(x, 4);
            outGPyramid[j].compute_root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
        }
        for (int j = 4; j < J; j++) {
            inGPyramid[j].compute_root().parallel(y);
            gPyramid[j].compute_root().parallel(k);
            outGPyramid[j].compute_root().parallel(y);
        }
    }

    output.compile_to_file("local_laplacian", levels, alpha, beta, input);

    return 0;
}

