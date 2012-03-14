#include <Halide.h>
using namespace Halide;

#include "../png.h"

#define PI 3.14159

Expr gauss1D(Expr x, Uniform<int> sigma) {
  Expr weight =  (1.0f / (sqrt(2.0f * PI)*sigma)*exp(-(x*x)/(2.0f*sigma*sigma)));
  return weight;
}

Expr gauss1D(Expr x, Expr sigma) {
  Expr weight =  (1.0f / (sqrt(2.0f * PI)*sigma)*exp(-(x*x)/(2.0f*sigma*sigma)));
  return weight;
}

Expr gauss2D(Expr x, Expr y, Uniform<int> sigma) {
  Expr weight =  (1.0f / (2.0f*PI*sigma*sigma)*exp(-(x*x + y*y)/(2.0f*sigma*sigma)));
  return weight;
}

Func bilateral(Func f, Uniform<int> sigmaS, Uniform<int>sigmaD_100) {
  // sigmaS is spatial sigma
  Var x, y, c, a, b;
  int cutoff = 3;
  RVar i(-cutoff*sigmaS, 1 + 2*cutoff*sigmaS),
    j(-cutoff*sigmaS, 1 + 2*cutoff*sigmaS);

  /*
  int xi, yi;
  xi = 30;
  yi = 30;
  */

  // (normalized) spatial gaussian
  Expr weightS = gauss2D(i, j, sigmaS);

  // gaussian in intensity

  Expr sigmaD = 0.2f; //0.01f*sigmaD_100;

  Func dI2;
  dI2(x, y, a, b) = pow(f(x, y, 0) - f(x+a, y+b, 0), 2) +
    pow(f(x, y, 1) - f(x+a, y+b, 1), 2) +
    pow(f(x, y, 1) - f(x+a, y+b, 1), 2);

  Func dI;
  dI(x, y, a, b) = sqrt(dI2(x, y, a, b));

  Func norm;
  norm(x, y, c) += gauss1D(dI(x, y, i, j), sigmaD) * weightS;
  //norm(x, y, c) = gauss1D(dI(xi, yi, x-xi, y-yi), sigmaD) * gauss2D(x-xi, y-yi, sigmaS);
  
  Func bilateral;
  bilateral(x, y, c) += gauss1D(dI(x, y, i, j), sigmaD) * weightS * f(x+i, y+j, c);

  Func normed;
  normed(x, y, c) = bilateral(x, y, c) / norm(x, y, c);

  return normed;
}

// Blur with gaussian with std dev sigma
Func gaussBlur(Func f, Uniform<int> sigma) {

  Var x, y, c;
  int cutoff = 3;
  RVar i(-cutoff*sigma, 1 + 2*cutoff*sigma);
  
  Expr weight = gauss1D(i, sigma);
  
  Func blur_x;
  blur_x(x, y, c) += weight*f(x + i, y, c);

  Func blur_y;
  blur_y(x, y, c) += weight*blur_x(x, y + i, c);
  
  // schedule

  /* this doesn't work /*
  blur_y.tile(x, y, xi, yi, 64, 64);
  blur_y.vectorize(xi, 8);
  blur_y.parallel(y);
  blur_x.chunk(x);
  blur_x.vectorize(x, 8);
  blur_x.chunk(xo);
  blur_x.split(x, xi, x, 1).split(y, yi, y, 1);
  blur_x.parallel(yo).parallel(yi).parallel(xo).parallel(xi);
  */
  blur_x.root();
  blur_y.root();

  return blur_y;
}

// Blur with variable size box

Func boxBlur(Func f, Uniform<int> k) {

  Var x, y, c;

  RVar i(-k, 2*k+1), j(-k, 2*k + 1);
  Func blur_x, blur_y;
  Expr norm = 2*k + 1;

  blur_x(x, y, c) += f(x + i, y, c) / norm;
  blur_y(x, y, c) += blur_x(x, y + j, c) / norm;

  return blur_y;
}

int main(int argc, char **argv) {

  //  Uniform<int> k;
  UniformImage input(UInt(16), 3);
  Uniform<int> k("k"), sigmaD("sigmaD_100");

  Var x("x"), y("y"), c("c"), xo("blockidx"), yo("blockidy"), 
    xi("threadidx"), yi("threadidy");

  // The algorithm

  // Convert to floating point
  Func floating;
  floating(x, y, c) = cast<float>(input(x, y, c)) / 65535.0f;

  // Set a boundary condition
  Func clamped;
  clamped(x, y, c) = floating(clamp(x, 0, input.width()-1), 
			      clamp(y, 0, input.height()-1), c);

  
  Func filter;
  //  filter = gaussBlur(clamped, k);
  filter = bilateral(clamped, k, sigmaD);

  // Convert back to 16-bit
  Func output;
  output(x, y, c) = cast<uint16_t>(clamp(filter(x, y, c), 0.0f, 1.0f) * 65535.0f);
  
  // schedule

  floating.root();
  //clamped.root();
  filter.root();
  output.root();

  output.compileToFile("local_laplacian");
  return 0;
}
