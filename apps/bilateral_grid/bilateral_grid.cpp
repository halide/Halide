#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    UniformImage input(UInt(16), 2);
    Uniform<float> r_sigma;
    Uniform<int> s_sigma;
    Var x("x"), y("y"), z("z"), c("c"), xi("xi"), yi("yi");

    // Convert the 16-bit input to floats
    Func floating;
    floating(x, y, c) = Cast<float>(input(x, y, c)) / 65535.0f;
    floating.root();

    // Take the luminance
    Func luminance;
    luminance(x, y) = floating(x, y, 0) * 0.299f + floating(x, y, 1) * 0.587f + floating(x, y, 2) * 0.114f;
    luminance.root();

    // Add a boundary condition 
    Func clamped;
    clamped(x, y) = luminance(Clamp(x, 0, input.width()),
                              Clamp(y, 0, input.height()));                                

    // Do linear splats to the grid
    RVar k(0, 2, "k");
    RVar i(0, s_sigma, "i"), j(0, s_sigma, "j");
    Expr val = clamped(x * s_sigma + i - s_sigma/2, y * s_sigma + j - s_sigma/2);
    Expr zv = val / r_sigma;
    Expr zi = Cast<int>(floor(zv));
    Expr zf = zv - floor(zv);
    Func grid("grid");
    grid(x, y, zi+k, c) += Select(k == 0, 1.0f-zf, zf) * Select(c == 0, val, 1.0f);

    // Blur the grid
    Func blurx, blury, blurz;
    blurx(x, y, z) = grid(x-1, y, z) + 2.0f*grid(x, y, z) + grid(x+1, y, z);
    blury(x, y, z) = blurx(x, y-1, z) + 2.0f*blurx(x, y, z) + blurx(x, y+1, z);
    blurz(x, y, z) = blury(x, y, z-1) + 2.0f*blury(x, y, z) + blury(x, y, z+1);

    blurz.root();

    // Take trilinear samples to compute the output in tiles
    val = Clamp(clamped(x*s_sigma + xi, y*s_sigma + yi), 0.0f, 1.0f);
    zv = val / r_sigma;
    zi = Cast<int>(floor(zv));
    zf = zv - floor(zv);
    Expr xf = Cast<float>(xi) / s_sigma;
    Expr yf = Cast<float>(yi) / s_sigma;
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

    // Remove tiles to get the result in homogeneous form
    Func homogeneous;
    homogeneous(x, y) = outTiles(x/s_sigma, y/s_sigma, x%s_sigma, y%s_sigma);

    homogeneous.root();

    // Normalize
    Func smoothed;
    smoothed(x, y) = homogeneous(x, y, 0)/homogeneous(x, y, 1);

    // Add clarity to the luminance channel by extrapolating away from the smoothed version
    Func clarified;
    clarified = 2.0f*luminance - smoothed;
    clarified.root();

    // reintroduce color
    Func color;
    color(x, y, c) = clarified(x, y) * floating(x, y, c) / luminance(x, y);
    color.root();

    // convert back to 16-bit
    Func output("clarity");
    output = Cast<uint16_t>(Clamp(color, 0.0f, 1.0f) * 65535.0f);

    output.compile();

    return 0;
}



