#include <Halide.h>
using namespace Halide;

#include "../png.h"

#define PI 3.14159

Expr gauss1D(RVar x, Uniform<int> sigma) {
  Expr weight =  (1.0f / (sqrt(2.0f * PI)*sigma)*exp(-(x*x)/(2.0f*sigma*sigma)));
  return weight;
}

Func bilateral(Func f, Uniform<int> sigmaR, Uniform<int> sigmaD) {
  
  Var x, y, c;
  int cutoff = 3;

  Func bilateral;
  
  return bilateral;
}

// Blur with gaussian with std dev sigma
Func gaussBlur(Func f, Uniform<int> sigma) {

  Var x, y, c;
  int cutoff = 3;
  RVar i(-cutoff*sigma, 1 + 2*cutoff*sigma),
    j(-cutoff*sigma, 1 + 2*cutoff*sigma);
  
  Expr weightX = gauss1D(i, sigma);
  Expr weightY = gauss1D(j, sigma);
  
  Func blur_x;
  blur_x(x, y, c) += weightX*f(x + i, y, c);

  Func blur_y;
  blur_y(x, y, c) += weightY*blur_x(x, y + j, c);
  
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
  Uniform<int> k;

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

  // Convert back to 16-bit
  Func output;
  output(x, y, c) = cast<uint16_t>(gaussBlur(clamped, k)(x, y, c) * 65535.0f);
  
  // schedule

  /*
  floating.root();
  blur_x.root();
  blur_y.root();
  output.root();
  */

  output.compileToFile("local_laplacian");
  return 0;
}
