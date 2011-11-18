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

    printf("Fread\n");
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            fread(&(im(x, y)), sizeof(float), 1, f);
        }
    }
    printf("Done\n");

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
    return x + ((x - y)/sigma) * (alpha * sigma) * gaussian((x - y)/sigma);
}

Func downsample(Func f) {
    Func simple;
    Var x, y;

    simple(x, y) = 0.25f * (f(2*x, 2*y) + f(2*x+1, 2*y) + f(2*x, 2*y+1) + f(2*x+1, 2*y+1));
    return simple;

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

    simple(x, y) = f(x/2, y/2);
    return simple;

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

Expr Clamp(Expr a, Expr min, Expr max) {
    return Select(a > max, max, Select(a < min, min, a));
}

int main(int argc, char **argv) {

    // intensity levels
    const int K = 8;

    // pyramid levels
    const int J = 4;
    
    // loop variables
    Var x("x"), y("y"), k("k"), j("j"), dx("dx"), dy("dy");

    Image<float> in = load(argv[1]);

    Func input("input");
    input(x, y) = in(Clamp(x, 0, in.width()-1), Clamp(y, 0, in.height()-1));
    
    printf("Defining processed gaussian pyramid\n");
    // Compute gaussian pyramids of the processed images. k is target intensity and j is pyramid level.
    Func gPyramid[J] = {"gp0", "gp1"};
    Func lPyramid[J] = {"lp0", "lp1"};
    Func inGPyramid[J] = {"igp0", "igp1"};
    gPyramid[0](x, y, k) = remap(input(x, y), Cast<float>(k) / (K-1), 1.0f, 1.0f / (K-1));
    for (int j = 1; j < J; j++)
        gPyramid[j] = downsample(gPyramid[j-1]);

    printf("Defining processed laplacian pyramid\n");
    // Compute laplacian pyramids of the processed images.
    lPyramid[J-1] = gPyramid[J-1];
    for (int j = J-2; j >= 0; j--)
        lPyramid[j] = gPyramid[j] - upsample(gPyramid[j+1]);    

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
        Expr level = inGPyramid[j](x, y) * float(K-1);
        Expr li = Clamp(Cast<int>(level), 0, K-1);
        Expr lf = level - Cast<float>(li);
        Expr e = (1.0f - lf) * lPyramid[j](x, y, li) + lf * lPyramid[j](x, y, li+1);
        outLPyramid[j](x, y) = e;
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
    uint32_t w = in.width();
    uint32_t h = in.height();
    //gPyramid[0].root(Range(-16, w) * Range(-16, h) * Range(0, K));    
    //gPyramid[1].root(Range(-6, w/2-4) * Range(-6, h/2-4) * Range(0, K));
    //gPyramid[2].root(Range(-1, w/4-6) * Range(-1, h/4-6) * Range(0, K));
    //lPyramid[1].root(Range(0, w/2-16) * Range(0, h/2-16) * Range(0, K));
    //lPyramid[0].root(Range(2, w - 36) * Range(2, h - 36) * Range(0, K));
    //outLPyramid[0].root(Range(0, in.width()-32) * Range(0, in.height()-32));
    //gPyramid[1].root(Range(-16, in.width()/2) * Range(-16, in.height()));

    input.root(Range(-128, w+256) * Range(-128, h+256));
    //outLPyramid[0].root(Range(0, w) * Range(0, h));
    //outGPyramid[1].root(Range(0, w/2) * Range(0, h/2));
    //outLPyramid[1].root(Range(0, w/2) * Range(0, h/2));
    //outLPyramid[2].root(Range(0, w/4) * Range(0, h/4));
    
    lPyramid[3].root(Range(0, w/8) * Range(0, h/8) * Range(0, K));
    lPyramid[2].root(Range(0, w/4) * Range(0, h/4) * Range(0, K));
    lPyramid[1].root(Range(0, w/2) * Range(0, h/2) * Range(0, K));
    lPyramid[0].root(Range(0, w) * Range(0, h) * Range(0, K));
    gPyramid[3].root(Range(0, w/8) * Range(0, h/8) * Range(0, K));
    gPyramid[2].root(Range(0, w/4) * Range(0, h/4) * Range(0, K));
    gPyramid[1].root(Range(0, w/2) * Range(0, h/2) * Range(0, K));
    gPyramid[0].root(Range(0, w) * Range(0, h) * Range(0, K));
    outLPyramid[3].root(Range(0, w/8) * Range(0, h/8));
    outLPyramid[2].root(Range(0, w/4) * Range(0, h/4));
    outLPyramid[1].root(Range(0, w/2) * Range(0, h/2));
    outLPyramid[0].root(Range(0, w) * Range(0, h));
    outGPyramid[3].root(Range(0, w/8) * Range(0, h/8));
    outGPyramid[2].root(Range(0, w/4) * Range(0, h/4));
    outGPyramid[1].root(Range(0, w/2) * Range(0, h/2));
    outGPyramid[0].root(Range(0, w) * Range(0, h));
    inGPyramid[3].root(Range(0, w/8) * Range(0, h/8));
    inGPyramid[2].root(Range(0, w/4) * Range(0, h/4));
    inGPyramid[1].root(Range(0, w/2) * Range(0, h/2));
    inGPyramid[0].root(Range(0, w) * Range(0, h));

    printf("Output has type %s\n", outGPyramid[0].returnType().str().c_str());

    //outGPyramid[0].trace();

    printf("Realizing...\n"); fflush(stdout);
    Image<float> out = outGPyramid[0].realize(w, h);

    save(out, argv[2]);

    return 0;
}

