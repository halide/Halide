#include <FImage.h>

using namespace FImage;

Image<float> load(const char *filename) {

    FILE *f = fopen(filename, "rb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h;

    fread(&h, sizeof(header_t), 1, f);

    Image<float> im(h.width, h.height);

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            fread(&(im(x, y)), sizeof(float), 1, f);
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
    } h {1, im.width(), im.height(), 1, 0};

    
    fwrite(&h, sizeof(header_t), 1, f);

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            fwrite(&im(x, y), sizeof(float), 1, f);
        }
    }

    fclose(f);
}

// We don't have exp in the language yet, so here's an approximation to a Gaussian
Expr gaussian(Expr x) {
    return 1.0f/(x*x+1.0f);
    
    x = Select(x < 0.0f, -x, x);
    Expr y = 2.0f - x;
    return Select(x < 1.0f, 4.0f - 6.0f*x*x + 3.0f*x*x*x,
                  Select(x < 2.0f, y*y*y, 0.0f)) * 0.25f;
}

// Remap x using y as the central point, an amplitude of alpha, and a std.dev of sigma
Expr remap(Expr x, Expr y, float alpha, float sigma) {
    return x + ((x - y)/sigma) * alpha * gaussian((x - y)/sigma);
}

Func downsample(Func f) {
    Func simple;
    Var x, y;

    //simple(x, y) = f(2*x, 2*y);
    //return simple;

    Func downx, downy;
    
    printf("Downsampling in x\n");
    // Downsample in x using 1 3 3 1
    downx(x, y) = (1.0f/8) * f(2*x-1, y) + (3.0f/8) * f(2*x, y) + (3.0f/8) * f(2*x+1, y) + (1.0f/8) * f(2*x+2, y);

    printf("Downsampling in y\n");
    // Downsample in y using 1 3 3 1
    downy(x, y) = (1.0f/8) * downx(x, 2*y-1) + (3.0f/8) * downx(x, 2*y) + (3.0f/8) * downx(x, 2*y+1) + (1.0f/8) * downx(x, 2*y+2);

    printf("Returning\n");                     
    return downy;
}

Func upsample(Func f) {
    Var x, y;
    Func simple;

    //simple(x, y) = f(x/2, y/2);
    //return simple;

    Func upx, upy;

    printf("Upsampling in x\n");
    // Upsample in x using linear interpolation
    upx(x, y) = 0.25f * f((x/2) - 1 + 2*(x % 2), y) + 0.75f * f(x/2, y);

    printf("Upsampling in y\n");
    // Upsample in y using linear interpolation
    upy(x, y) = 0.25f * upx(x, (y/2) - 1 + 2*(y % 2)) + 0.75f * upx(x, y/2);

    printf("Returning\n");
    return upy;
}

int main(int argc, char **argv) {

    // intensity levels
    const int K = 3;

    // pyramid levels
    const int J = 3;
    
    // loop variables
    Var x("x"), y("y"), k("k"), j("j"), dx("dx"), dy("dy");

    Image<float> in = load(argv[1]);

    Func input("input");
    input(x, y) = in(x+16, y+16);
    
    printf("Defining processed gaussian pyramid\n");
    // Compute gaussian pyramids of the processed images. k is target intensity and j is pyramid level.
    Func gPyramid[J] = {"gp0", "gp1"};
    Func lPyramid[J] = {"lp0", "lp1"};
    Func inGPyramid[J] = {"igp0", "igp1"};
    gPyramid[0](x, y, k) = remap(input(x, y), Cast<float>(k) / (K-1), 1, 1.0f / (K-1));
    for (int j = 1; j < J; j++)
        gPyramid[j] = downsample(gPyramid[j-1]);

    printf("Defining processed laplacian pyramid\n");
    // Compute laplacian pyramids of the processed images.
    lPyramid[J-1] = gPyramid[J-1];
    for (int j = J-2; j >= 0; j--)
        lPyramid[j] = gPyramid[j] - upsample(lPyramid[j+1]);    

    printf("Defining gaussian pyramid of input\n");
    // Compute gaussian and laplacian pyramids of the input
    inGPyramid[0] = input;
    for (int j = 1; j < J; j++)
        inGPyramid[j] = downsample(inGPyramid[j-1]);

    /*
    printf("Defining laplacian pyramid of input\n");
    inLPyramid[J-1] = inGPyramid[J-1];
    for (int j = J-2; j >= 0; j--) 
        inLPyramid[j] = inGPyramid[j] - upsample(inLPyramid[j+1]);
    */

    // Contruct the laplacian pyramid of the output, by blending
    // between the processed laplacian pyramids
    printf("Defining laplacian pyramid of output\n");
    Func outLPyramid[J] = {"olp0", "olp1"};
    for (int j = 0; j < J; j++) {
        // Split into integer and floating parts
        Expr li = Cast<int>(inGPyramid[j](x, y)), lf = inGPyramid[j](x, y) - Cast<float>(li);
        outLPyramid[j](x, y) = (1.0f - lf) * lPyramid[j](x, y, li) + lf * lPyramid[j](x, y, li+1);
    }

    printf("Defining gaussian pyramid of output\n");
    // Collapse output pyramid
    Func outGPyramid[J] = {"ogp0", "ogp1"};
    outGPyramid[J-1] = outLPyramid[J-1];
    for (int j = J-2; j >= 0; j--) 
        outGPyramid[j] = upsample(outGPyramid[j+1]) + outLPyramid[j];

    // Dependencies:
    // outGPyramid[0]: outLPyramid[0], outLPyramid[1]
    // outLPyramid[0]: lPyramid[0], inGPyramid[0]
    // outLPyramid[1]: lPyramid[1], inGPyramid[1]
    // inGPyramid[0]:  input
    // inGPyramid[1]:  inGPyramid[0]
    // lPyramid[0]:    gPyramid[0], lPyramid[1]
    // lPyramid[1]:    gPyramid[1]
    // gPyramid[0]:    input
    // gPyramid[1]:    gPyramid[0]

    printf("Specifying schedule...\n");
    for (int j = 0; j < J; j++) {
        uint32_t w = in.width() >> j;
        uint32_t h = in.height() >> j;

        //gPyramid[j].chunk(y, Range(0, w)*Range(0, h)*Range(0, K));
        //lPyramid[j].chunk(y, Range(0, w)*Range(0, h)*Range(0, K));
        //inGPyramid[j].chunk(y, Range(0, w)*Range(0, h));
        //outLPyramid[j].chunk(y, Range(0, w)*Range(0, h));
        //if (j > 0) outGPyramid[j].chunk(y, Range(0, w)*Range(0, h));
    }

    printf("Output has type %s\n", outGPyramid[0].returnType().str().c_str());

    //outGPyramid[0].trace();

    printf("Realizing...\n"); fflush(stdout);
    Image<float> out = outGPyramid[0].realize(in.width()-32, in.height()-32);

    save(out, argv[2]);

    return 0;
}

