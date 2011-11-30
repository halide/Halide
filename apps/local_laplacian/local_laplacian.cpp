#include <FImage.h>
#include <sys/time.h>

using namespace FImage;

Image<float> load(const char *filename) {

    FILE *f = fopen(filename, "rb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h;

    fread(&h, sizeof(header_t), 1, f);

    Image<float> im(h.width, h.height, h.channels);
    
    printf("Fread\n");
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            for (int c = 0; c < im.channels(); c++) {
                fread(&(im(x, y, c)), sizeof(float), 1, f);
            }
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

// We don't have exp in the language yet, so here's a very rough approximation to a Gaussian
Expr gaussian(Expr x) {
    return 1.0f/(x*x+1.0f);
}

// Remap x using y as the central point, an amplitude of alpha, and a std.dev of sigma
Expr remap(Expr x, Expr y, float alpha, float beta, float sigma) {
    return y + beta*(x - y) + ((x - y)/sigma) * (alpha * sigma) * gaussian((x - y)/sigma);
}

Func downsample(Func f) {
    Var x, y;
    Func downx, downy;
    
    printf("Downsampling in x\n");
    downx(x, y) = (f(2*x-1, y) + 3.0f * (f(2*x, y) + f(2*x+1, y)) + f(2*x+2, y)) / 8.0f;

    printf("Downsampling in y\n");
    downy(x, y) = (downx(x, 2*y-1) + 3.0f * (downx(x, 2*y) + downx(x, 2*y+1)) + downx(x, 2*y+2)) / 8.0f;

    downx.root();
    downy.root();

    return downy;
}

Func upsample(Func f) {
    Var x, y;
    Func upx, upy;

    //upy(x, y) = f(x/2, y/2);
    //return upy;

    printf("Upsampling in x\n");
    upx(x, y) = 0.25f * f((x/2) - 1 + 2*(x % 2), y) + 0.75f * f(x/2, y);

    printf("Upsampling in y\n");
    upy(x, y) = 0.25f * upx(x, (y/2) - 1 + 2*(y % 2)) + 0.75f * upx(x, y/2);

    upx.root();
    upy.root();

    printf("Returning\n");
    return upy;
}

class LocalLaplacian {
public:
    UniformImage input;
    Func output;

    LocalLaplacian(int K, int J, float alpha, float beta, int seed) : input(Float(32), 3) {

        // K is intensity levels
        // J is pyramid levels
    
        // loop variables
        Var x, y, c, k;
        
        printf("Setting boundary condition\n");
        Func clamped("clamped");
        clamped(x, y, c) = input(Clamp(x, 0, input.width()-1), Clamp(y, 0, input.height()-1), c);
        
        printf("Defining luminance\n");
        Func gray("gray");
        gray(x, y) = 0.299f * clamped(x, y, 0) + 0.587f * clamped(x, y, 1) + 0.114f * clamped(x, y, 2);
        
        printf("Defining processed gaussian pyramid\n");
        Func gPyramid[J];
        gPyramid[0](x, y, k) = remap(gray(x, y), Cast<float>(k) / (K-1), alpha, beta, 1.0f / (K-1));
        for (int j = 1; j < J; j++)
            gPyramid[j] = downsample(gPyramid[j-1]);
        
        printf("Defining processed laplacian pyramid\n");
        Func lPyramid[J];
        lPyramid[J-1] = gPyramid[J-1];
        for (int j = J-2; j >= 0; j--)
            lPyramid[j] = gPyramid[j] - upsample(gPyramid[j+1]);    
        
        printf("Defining gaussian pyramid of input\n");
        Func inGPyramid[J];
        inGPyramid[0] = gray;
        for (int j = 1; j < J; j++)
            inGPyramid[j] = downsample(inGPyramid[j-1]);
        
        printf("Defining laplacian pyramid of output\n");
        Func outLPyramid[J];
        for (int j = 0; j < J; j++) {
            // Split input pyramid value into integer and floating parts
            Expr level = inGPyramid[j](x, y) * float(K-1);
            Expr li = Clamp(Cast<int>(level), 0, K-2);
            Expr lf = level - Cast<float>(li);
            // Linearly interpolate between the nearest processed pyramid levels
            outLPyramid[j](x, y) = (1.0f - lf) * lPyramid[j](x, y, li) + lf * lPyramid[j](x, y, li+1);
        }
        
        printf("Defining gaussian pyramid of output\n");
        Func outGPyramid[J];
        outGPyramid[J-1] = outLPyramid[J-1];
        for (int j = J-2; j >= 0; j--) 
            outGPyramid[j] = upsample(outGPyramid[j+1]) + outLPyramid[j];
        
        printf("Reintroducing color\n");

        output(x, y, c) = outGPyramid[0](x, y) * clamped(x, y, c) / gray(x, y);
        
        printf("Specifying a random schedule...\n");
        std::vector<Func> funcs;
        funcs.push_back(clamped);
        funcs.push_back(gray);
        funcs.insert(funcs.end(), lPyramid, lPyramid+J);
        funcs.insert(funcs.end(), gPyramid, gPyramid+J);
        funcs.insert(funcs.end(), outLPyramid, outLPyramid+J);        
        funcs.insert(funcs.end(), outGPyramid, outGPyramid+J);        
        funcs.insert(funcs.end(), inGPyramid, inGPyramid+J);

        for (size_t i = 0; i < funcs.size(); i++) {
            int decision = rand() % 3;
            switch (rand() % 3) {
            case 1:
                // inline
                printf("Scheduling %s as inline\n", funcs[i].name().c_str());
                break;
            default:
                // root
                printf("Scheduling %s as root\n", funcs[i].name().c_str());
                funcs[i].root();            
            }
        }
        
        //output.trace();
        
        printf("Compiling...\n"); fflush(stdout);
        // output(x, y, c) = outGPyramid[0];
        output.compile();
    }
    
    Image<float> operator()(Image<float> im) {
        input = im;
        Image<float> out = output.realize(im.width(), im.height(), im.channels());
        printf("input = %f, output = %f\n", im(0, 0, 0), out(0, 0, 0));
        return out;
    }
};


int main(int argc, char **argv) {

    Image<float> im = load(argv[1]);
    LocalLaplacian ll(8, 8, atof(argv[3]), atof(argv[4]), 1);
    printf("%f\n", im(0, 0, 0));
    Image<float> out = ll(im);
    save(out, argv[2]);
    
    printf("Done\n");
    return 0;
}

