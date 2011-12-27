#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    UniformImage input(UInt(16), 2);
    Uniform<float> r_sigma;
    Uniform<int> s_sigma;
    Var x("x"), y("y"), z("z"), c("c"), xi("xi"), yi("yi");

    // Add a boundary condition 
    Func clamped;
    clamped(x, y) = input(Clamp(x, 0, input.width()),
                          Clamp(y, 0, input.height()));                                

    // Scale the input
    Func in;
    in(x, y) = Cast<float>(clamped(x, y))/r_sigma;

    // Do linear splats to the grid
    printf("Splat\n");
    RVar k(0, 2, "k");
    RVar i(0, s_sigma, "i"), j(0, s_sigma, "j");
    Expr val = in(x * s_sigma + i - s_sigma/2, y * s_sigma + j - s_sigma/2);
    Expr weight = val - floor(val);
    Func grid("grid");
    grid(x, y, Cast<int>(floor(val))+k, c) += Select(k == 1, weight, (1.0f-weight)) * Select(c == 0, val, 1.0f);

    // Blur the grid
    printf("Blur\n");
    Func blurx, blury, blurz;
    blurx(x, y, z) = grid(x-1, y, z) + 2.0f*grid(x, y, z) + grid(x+1, y, z);
    blury(x, y, z) = blurx(x, y-1, z) + 2.0f*blurx(x, y, z) + blurx(x, y+1, z);
    blurz(x, y, z) = blury(x, y, z-1) + 2.0f*blury(x, y, z) + blury(x, y, z+1);

    // Take trilinear slices
    printf("Slice\n");
    val = Clamp(in(x*s_sigma + xi, y*s_sigma + yi), 0.0f, 16.0f);
    Expr xf = Cast<float>(xi) / s_sigma;
    Expr yf = Cast<float>(yi) / s_sigma;
    Expr zi = Cast<int>(floor(val));
    Expr zf = val - floor(val);
    Func outTiles;
    outTiles(x, y, xi, yi, c) = 
        (blurz(x, y, zi, c) * (1.0f - xf) * (1.0f - yf) * (1.0f - zf) + 
         blurz(x+1, y, zi, c) * xf * (1.0f - yf) * (1.0f - zf) + 
         blurz(x, y+1, zi, c) * (1.0f - xf) * yf * (1.0f - zf) + 
         blurz(x+1, y+1, zi, c) * xf * yf * (1.0f - zf) + 
         blurz(x, y, zi+1, c) * (1.0f - xf) * (1.0f - yf) * zf + 
         blurz(x+1, y, zi+1, c) * xf * (1.0f - yf) * zf + 
         blurz(x, y+1, zi+1, c) * (1.0f - xf) * yf * zf + 
         blurz(x+1, y+1, zi+1, c) * xf * yf * zf);

    // Remove tiles
    Func homogeneous;
    homogeneous(x, y) = outTiles(x/s_sigma, y/s_sigma, x%s_sigma, y%s_sigma); 

    // Normalize
    printf("Normalize\n");
    Func normalized("bilateral_grid");
    normalized(x, y) = Cast<uint16_t>(homogeneous(x, y, 0)*r_sigma/homogeneous(x, y, 1));

    normalized.compile();

    return 0;
}



