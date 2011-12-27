#include <FImage.h>

using namespace FImage;

// We don't have exp in the language yet, a rough approximation to a Gaussian-like function
Expr gaussian(Expr x) {
    return 1.0f/(x*x+1.0f);
}

// Remap x using y as the central point, an amplitude of alpha, and a std.dev of sigma
Expr remap(Expr x, Expr y, Expr alpha, Expr beta, Expr sigma) {
    return y + beta*(x - y) + ((x - y)/sigma) * (alpha * sigma) * gaussian((x - y)/sigma);
}

Func downsample(Func f) {
    Var x, y;
    Func downx, downy;
    
    printf("Downsampling in x\n");
    downx(x, y) = (f(2*x-1, y) + 3.0f * (f(2*x, y) + f(2*x+1, y)) + f(2*x+2, y)) / 8.0f;

    printf("Downsampling in y\n");
    downy(x, y) = (downx(x, 2*y-1) + 3.0f * (downx(x, 2*y) + downx(x, 2*y+1)) + downx(x, 2*y+2)) / 8.0f;

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

    printf("Returning\n");
    return upy;
}

int main(int argc, char **argv) {

    // Args: number of pyramid levels to compile for, scheduling seed
    int J = atoi(argv[1]);
    int seed = atoi(argv[2]);

    // number of intensity levels
    Uniform<int> levels("levels");
    // Parameters controlling the filter
    Uniform<float> alpha("alpha"), beta("beta");
    // Takes a 16-bit input
    UniformImage input(UInt(16), 3);

    assert(J <= 8);

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
    Func gPyramid[] = {"gp0", "gp1", "gp2", "gp3", "gp4", "gp5", "gp6", "gp7"};
    gPyramid[0](x, y, k) = remap(gray(x, y), Cast<float>(k) / (levels-1), alpha, beta, 1.0f / (levels-1));
    for (int j = 1; j < J; j++)
        gPyramid[j](x, y, k) = downsample(gPyramid[j-1])(x, y, k);
    
    // Get its laplacian pyramid
    Func lPyramid[] = {"lp0", "lp1", "lp2", "lp3", "lp4", "lp5", "lp6", "lp7"};
    lPyramid[J-1] = gPyramid[J-1];
    for (int j = J-2; j >= 0; j--)
        lPyramid[j](x, y, k) = gPyramid[j](x, y, k) - upsample(gPyramid[j+1])(x, y, k);    
        
    // Make the Gaussian pyramid of the input
    Func inGPyramid[] = {"igp0", "igp1", "igp2", "igp3", "igp4", "igp5", "igp6", "igp7"};
    inGPyramid[0] = gray;
    for (int j = 1; j < J; j++)
        inGPyramid[j](x, y) = downsample(inGPyramid[j-1])(x, y);
        
    // Make the laplacian pyramid of the output
    Func outLPyramid[] = {"olp0", "olp1", "olp2", "olp3", "olp4", "olp5", "olp6", "olp7"};
    for (int j = 0; j < J; j++) {
        // Split input pyramid value into integer and floating parts
        Expr level = inGPyramid[j](x, y) * Cast<float>(levels-1);
        Expr li = Clamp(Cast<int>(level), 0, levels-2);
        Expr lf = level - Cast<float>(li);
        // Linearly interpolate between the nearest processed pyramid levels
        outLPyramid[j](x, y) = (1.0f - lf) * lPyramid[j](x, y, li) + lf * lPyramid[j](x, y, li+1);
    }
    
    // Make the Gaussian pyramid of the output
    Func outGPyramid[] = {"ogp0", "ogp1", "ogp2", "ogp3", "ogp4", "ogp5", "ogp6", "ogp7"};
    outGPyramid[J-1] = outLPyramid[J-1];
    for (int j = J-2; j >= 0; j--) 
        outGPyramid[j](x, y) = upsample(outGPyramid[j+1])(x, y) + outLPyramid[j](x, y);
    
    // Reintroduce color
    Func color;
    color(x, y, c) = outGPyramid[0](x, y) * clamped(x, y, c) / gray(x, y);
        
    Func output("local_laplacian");
    // Convert back to 16-bit
    output(x, y, c) = Cast<uint16_t>(Clamp(color(x, y, c), 0.0f, 1.0f) * 65535.0f);

    // Specify a random schedule
    srand(seed);
    std::vector<Func> funcs = output.rhs().funcs();
    Var xo, xi;
    for (size_t i = 0; i < funcs.size(); i++) {
        switch (rand() % 5) {
        case 0:
            // inline
            printf("Scheduling %s as inline\n", funcs[i].name().c_str());
            break;
        case 1: 
            // root 
            printf("Scheduling %s as root\n", funcs[i].name().c_str());
            funcs[i].root();            
            break;
        case 2: 
            // root and vectorize
            printf("Scheduling %s as root and vectorized over x\n", funcs[i].name().c_str());
            funcs[i].root();
            funcs[i].split(x, xo, xi, 4);
            funcs[i].vectorize(xi);
            break;
        case 3:
            // Chunk over x
            printf("Scheduling %s as chunked over y\n", funcs[i].name().c_str());
            funcs[i].chunk(y);                
            break;
        case 4:
            // Chunk over x and vectorize
            printf("Scheduling %s as chunked over y and vectorized over x\n", funcs[i].name().c_str());
            funcs[i].chunk(y);
            funcs[i].split(x, xo, xi, 4);
            funcs[i].vectorize(xi);
            break;
        default:
            printf("How did I get here?\n");
            exit(-1);
        }
    }
    
    output.compile();
    
    return 0;
}

