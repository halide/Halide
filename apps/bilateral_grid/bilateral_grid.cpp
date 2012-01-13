#include "FImage.h"

using namespace FImage;

Expr lerp(Expr a, Expr b, Expr alpha) {
    return (1.0f - alpha)*a + alpha*b;
}

int main(int argc, char **argv) {
    UniformImage input(Float(32), 2);
    Uniform<float> r_sigma;
    Uniform<int> s_sigma;
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
    grid(x, y, z, c) = 0.0f;
    grid(x, y, zi, c) += Select(c == 0, val, 1.0f);

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
    interpolated(xi, yi, x, y, c) = 
        lerp(lerp(lerp(blurz(x, y, zi, c), blurz(x+1, y, zi, c), xf),
                  lerp(blurz(x, y+1, zi, c), blurz(x+1, y+1, zi, c), xf), yf),
             lerp(lerp(blurz(x, y, zi+1, c), blurz(x+1, y, zi+1, c), xf),
                  lerp(blurz(x, y+1, zi+1, c), blurz(x+1, y+1, zi+1, c), xf), yf), zf);

    Func outTiles;
    outTiles(xi, yi, x, y) = interpolated(xi, yi, x, y, 0) / interpolated(xi, yi, x, y, 1);

    // Remove tiles to get the result
    Func smoothed("smoothed");
    smoothed(x, y) = outTiles(x%s_sigma, y%s_sigma, x/s_sigma, y/s_sigma);

    grid.root().parallel(z);
    grid.update().transpose(y, c).transpose(x, c).transpose(i, c).transpose(j, c).parallel(y);
    blurx.root().parallel(z).vectorize(x, 4);
    blury.root().parallel(z).vectorize(x, 4);
    blurz.root().parallel(z).vectorize(x, 4);
    smoothed.root().parallel(y); 

    smoothed.compileToFile("bilateral_grid");

    // Compared to Sylvain Paris' implementation from his webpage (on
    // which this is based), for filter params 8 0.1, on a 4 megapixel
    // input, on a four core x86 (2 socket core2 mac pro)
    // Filter s_sigma: 2      4       8       16      32
    // Paris (ms):     5350   1345    472     245     184
    // Us (ms):        425    150     80.8    66.6    68.7
    // Speedup:        12.5   9.0     5.9     3.7     2.7

    // Our schedule and inlining are roughly the same as his, so the
    // gain is all down to vectorizing and parallelizing. In general
    // for larger blurs our win shrinks to roughly the number of
    // cores, as the stages we don't vectorize dominate.  For smaller
    // blurs, our win grows, because the stages that we vectorize take
    // up all the time.
    

    return 0;
}



