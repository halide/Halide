#include "FImage.h"

using namespace FImage;

Expr lerp(Expr a, Expr b, Expr alpha) {
    return (1.0f - alpha)*a + alpha*b;
}

int main(int argc, char **argv) {
    UniformImage input(Float(32), 2);
    Uniform<float> r_sigma;
    int s_sigma = atoi(argv[1]);
    Var x("x"), y("y"), z("z"), c("c"), xi("xi"), yi("yi");

    // Add a boundary condition 
    Func clamped("clamped");
    clamped(x, y) = input(Clamp(x, 0, input.width()),
                          Clamp(y, 0, input.height()));                                

    // Construct the bilateral grid 
    RVar i(0, s_sigma, "i"), j(0, s_sigma, "j");
    Expr val = clamped(x * s_sigma + i - s_sigma/2, y * s_sigma + j - s_sigma/2);
    val = Clamp(val, 0.0f, 1.0f);
    Expr zi = Cast<int>(val * (1.0f/r_sigma) + 0.5f);
    Func grid("grid");
    printf("A\n");
    grid(x, y, z, c) = 0.0f;
    printf("B\n");
    grid(x, y, zi, c) += Select(c == 0, val, 1.0f);
    printf("C\n");

    // Blur the grid using a five-tap filter
    Func blurx("blurx"), blury("blury"), blurz("blurz");
    blurx(x, y, z) = grid(x-2, y, z) + grid(x-1, y, z)*4 + grid(x, y, z)*6 + grid(x+1, y, z)*4 + grid(x+1, y, z);
    blury(x, y, z) = blurx(x, y-2, z) + blurx(x, y-1, z)*4 + blurx(x, y, z)*6 + blurx(x, y+1, z)*4 + blurx(x, y+2, z);
    blurz(x, y, z) = blury(x, y, z-2) + blury(x, y, z-1)*4 + blury(x, y, z)*6 + blury(x, y, z+1)*4 + blury(x, y, z+2);

    // Take trilinear samples to compute the output in tiles
    val = clamped(x*s_sigma + xi, y*s_sigma + yi);
    val = Clamp(val, 0.0f, 1.0f);
    Expr zv = val * (1.0f/r_sigma);
    zi = Cast<int>(zv);
    Expr zf = zv - zi;
    Expr xf = Cast<float>(xi) / s_sigma;
    Expr yf = Cast<float>(yi) / s_sigma;

    Func interpolated("interpolated");    

    interpolated(x, y, xi, yi, c) = 
        lerp(lerp(lerp(blurz(x, y, zi, c), blurz(x+1, y, zi, c), xf),
                  lerp(blurz(x, y+1, zi, c), blurz(x+1, y+1, zi, c), xf),
                  yf),
             lerp(lerp(blurz(x, y, zi+1, c), blurz(x+1, y, zi+1, c), xf),
                  lerp(blurz(x, y+1, zi+1, c), blurz(x+1, y+1, zi+1, c), xf),
                  yf),
             zf);

    /*
      // Precompute the lerp weights
    Func bilerpWeight;
    bilerpWeight(xi, yi) = xf*yf;
    bilerpWeight.root();
    
    
    interpolated(x, y, xi, yi, c) = 
        (bilerpWeight(s_sigma-xi-1, s_sigma-yi-1) * lerp(blurz(x,   y,   zi, c), blurz(x,   y, zi+1, c), zf) + 
         bilerpWeight(xi, s_sigma-yi-1) * lerp(blurz(x+1, y,   zi, c), blurz(x+1, y, zi+1, c), zf) + 
         bilerpWeight(s_sigma-xi-1, yi) * lerp(blurz(x,   y+1, zi, c), blurz(x,   y+1, zi+1, c), zf) + 
         bilerpWeight(xi, yi) * lerp(blurz(x+1, y+1, zi, c), blurz(x+1, y+1, zi+1, c), zf));
    */

    Func outTiles;
    outTiles(x, y, xi, yi) = interpolated(x, y, xi, yi, 0) / interpolated(x, y, xi, yi, 1);

    // Remove tiles to get the result
    Func smoothed("smoothed");
    smoothed(x, y) = outTiles(x/s_sigma, y/s_sigma, x%s_sigma, y%s_sigma);

    grid.root();
    blurx.root();
    blury.root();
    blurz.root();
    smoothed.root();

    smoothed.compileToFile("bilateral_grid");

    return 0;
}



