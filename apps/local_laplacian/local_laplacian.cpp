#include <FImage.h>

using namespace FImage;

Image<float> load(const char *filename) {

    FILE *f = fopen(filename, "rb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h;

    fread(&h, sizeof(header_t), 1, f);

    Image<float> im(h.width, h.height, h.channels);

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            for (int c = 0; c < im.channels(); c++) {
                fread(&im(x, y, c), sizeof(float), 1, f);
            }
        }
    }

    fclose(f);
    return im;
}

void save(Image<float> im, const char *filename) {
    FILE *f = fopen(filename, "wb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h {1, im.width(), im.height(), im.channels(), 0};

    
    fwrite(&h, sizeof(header_t), 1, f);

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            for (int c = 0; c < im.channels(); c++) {
                fwrite(&im(x, y, c), sizeof(float), 1, f);
            }
        }
    }

    fclose(f);
}

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
    
    printf("Downsampling in x\n");
    // Downsample in x using 1 3 3 1
    downx(x, y) = f(2*x-1, y) + 3*f(2*x, y) + 3*f(2*x+1, y) + f(2*x+2, y);

    printf("Downsampling in y\n");
    // Downsample in y using 1 3 3 1
    downy(x, y) = downx(x, 2*y-1) + 3*downx(x, 2*y) + 3*downx(x, 2*y+1) + downx(x, 2*y+2);

    printf("Renormalizing\n");
    // Normalize
    scaled = downy / 64.0f;

    printf("Returning\n");                     
    return scaled;
}

Func upsample(Func f) {
    Var x, y;
    Func upx, upy, scaled;

    printf("Upsampling in x\n");
    // Upsample in x using linear interpolation
    upx(x, y) = f((x/2) - 1 + 2*(x % 2), y) + 3*f(x/2, y);

    printf("Upsampling in y\n");
    // Upsample in y using linear interpolation
    upy(x, y) = upx(x, (y/2) - 1 + 2*(y % 2)) + 3*f(x, y/2);

    printf("Renormalizing\n");
    // Normalize
    scaled = upy / 16.0f;

    printf("Returning\n");
    return scaled;
}

int main(int argc, char **argv) {

    // intensity levels
    const int K = 8;

    // pyramid levels
    const int J = 8;
    
    // loop variables
    Var x("x"), y("y"), k("k"), j("j"), dx("dx"), dy("dy");

    Image<float> in = load(argv[1]);

    Func input("input");
    input(x, y) = in(x+4, y+4);
    
    printf("Defining processed gaussian pyramid\n");
    // Compute gaussian pyramids of the processed images. k is target intensity and j is pyramid level.
    Func gPyramid[J], lPyramid[J], inGPyramid[J], inLPyramid[J];
    gPyramid[0](x, y, k) = remap(input(x, y), Cast<float>(k) / (K-1), 1, 1.0f / (K-1));
    for (int j = 1; j < J; j++)
        gPyramid[j](x, y, k) = downsample(gPyramid[j-1])(x, y, k);

    printf("Defining processed laplacian pyramid\n");
    // Compute laplacian pyramids of the processed images.
    lPyramid[J-1](x, y, k) = gPyramid[J-1](x, y, k);
    for (int j = J-2; j >= 0; j--)
        lPyramid[j](x, y, k) = gPyramid[j](x, y, k) - upsample(lPyramid[j+1])(x, y, k);

    printf("Defining gaussian pyramid of input\n");
    // Compute gaussian and laplacian pyramids of the input
    inGPyramid[0](x, y) = input(x, y);
    for (int j = 1; j < J; j++)
        inGPyramid[j](x, y) = downsample(inGPyramid[j-1])(x, y);

    printf("Defining laplacian pyramid of input\n");
    inLPyramid[J-1](x, y) = inGPyramid[J-1](x, y);
    for (int j = J-2; j >= 0; j--) 
        inLPyramid[j](x, y) = inGPyramid[j](x, y) - upsample(inLPyramid[j+1])(x, y);
    
    // Contruct the laplacian pyramid of the output, by blending
    // between the processed laplacian pyramids
    printf("Defining laplacian pyramid of output\n");
    Func outLPyramid[J];
    Expr l = inGPyramid[J](x, y);
    // Split into integer and floating parts
    Expr li = Cast<int>(inGPyramid[J](x, y)), lf = inGPyramid[J](x, y) - Cast<float>(li);
    for (int j = 0; j < J; j++) 
        outLPyramid[j](x, y) = (1 - lf) * lPyramid[j](x, y, li) + lf * lPyramid[j](x, y, li+1);

    printf("Defining gaussian pyramid of output\n");
    // Collapse output pyramid
    Func outGPyramid[J];
    outGPyramid[J-1](x, y) = outLPyramid[J-1](x, y);
    for (int j = J-2; j >= 0; j--) 
        outGPyramid[j](x, y) = upsample(outGPyramid[j+1])(x, y) + outLPyramid[j](x, y);

    printf("Realizing...\n");
    Image<float> out = outGPyramid[0].realize(in.width()-8, in.height()-8);

    save(out, argv[2]);

    return 0;
}

