#include <FImage.h>

using namespace FImage;

// We don't have exp in the language yet, so here's a polynomial approximation to a Gaussian
Expr gaussian(Expr x) {
    x = Select(x < 0.0f, -x, x);
    Expr y = 2.0f - x;
    return Select(x < 1.0f, 4.0f - 6.0f*x*x + 3.0f*x*x*x,
                  Select(x < 2.0f, y*y*y, 0.0f)) * 0.25f;
}

// Remap x using y as the central point, an amplitude of alpha, and a std.dev of sigma
Expr remap(Expr x, Expr y, float alpha, float sigma) {
    return x + x * alpha * gaussian((x - y)/sigma);
}

Func downsample(Func f) {
    Func downx, downy, scaled;
    Var x, y;
 
    // Downsample in x using 1 3 3 1
    downx(x, y) = f(2*x-1, y) + 3*f(2*x, y) + 3*f(2*x+1, y) + f(2*x+2, y);

    // Downsample in y using 1 3 3 1
    downy(x, y) = downx(x, 2*y-1) + 3*downx(x, 2*y) + 3*downx(x, 2*y+1) + downx(x, 2*y+2);

    // Normalize
    scaled = downy / 64;
                     
    return scaled;
}

Func upsample(Func f) {
    Var x, y;
    Func upx, upy, scaled;

    // Upsample in x using linear interpolation
    upx(2*x, y) = f(x-1, y) + 3*f(x, y);
    upx(2*x+1, y) = f(x+1, y) + 3*f(x, y);

    // Upsample in y using linear interpolation
    upy(x, 2*y) = upx(x, y-1) + 3*upx(x, y);
    upy(x, 2*y+1) = upx(x, y+1) + 3*upx(x, y);

    // Normalize
    scaled = upy / 16;

    return scaled;
}

int main(int argc, char **argv) {

    // intensity levels
    const int K = 8;

    // pyramid levels
    const int J = 8;
    
    // loop variables
    Var x("x"), y("y"), k("k"), j("j"), dx("dx"), dy("dy");
    
    // Compute gaussian pyramids of the processed images. k is target intensity and j is pyramid level.
    Func gPyramid[J], lPyramid[J], inGPyramid[J], inLPyramid[J];
    gPyramid[0](x, y, k) = remap(input(x, y), Cast<float>(k) / (K-1), 1, 1.0f / (K-1));
    for (int j = 1; j < J; j++)
        gPyramid[j](x, y, k) = downsample(gPyramid[j-1])(x, y, k);

    // Compute laplacian pyramids of the processed images.
    lPyramid[J-1](x, y, k) = gPyramid[J-1](x, y, k);
    for (int j = J-2; j >= 0; j--)
        lPyramid[j](x, y, k) = gPyramid[J](x, y, k) - upsample(lPyramid[J+1])(x, y, k);

    // Compute gaussian and laplacian pyramids of the input
    inGPyramid[0](x, y, 0) = input(x, y);
    for (int j = 1; j < J; j++)
        inGPyramid[j](x, y) = downsample(inGPyramid[J-1])(x, y, k);

    inLPyramid[J-1](x, y) = inGPyramid(x, y, J-1);
    for (int j = J-2; j >= 0; j--) 
        inLPyramid[j](x, y) = inGPyramid[j](x, y) - upsample(inLPyramid[J+1])(x, y);
    
    // Contruct the laplacian pyramid of the output, by blending
    // between the processed laplacian pyramids
    Func outLPyramid[J];
    Expr l = inGPyramid[J](x, y);
    // Split into integer and floating parts
    Expr li = Floor(inGPyramid[J](x, y)), lf = inGPyramid[J](x, y) - Cast<float>(li);
    for (int j = 0; j < J; j++) 
        outLPyramid[j](x, y) = (1 - lf) * lPyramid[j](x, y, li) + lf * lPyramid[j](x, y, li+1);

    // Collapse output pyramid
    Func outGPyramid[J];
    outGPyramid[J-1](x, y) = outLPyramid(x, y);
    for (int j = J-2; j >= 0; j--) 
        outGPyramid[j](x, y) = upsample(outGPyramid[j+1])(x, y) + outLPyramid[j](x, y);

    return outGPyramid[0];
}

