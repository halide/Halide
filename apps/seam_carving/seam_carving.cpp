#include <Halide.h>
using namespace Halide;

#include "../png.h"

#define PI 3.14159

Func gradientMagnitude(Func f, Expr width, Expr height) {

  const int margin = 2;
  Var x("x"), y("y");
  Expr nearTop = y < margin;
  Expr nearBottom = y >= height - margin;
  Expr nearLeft = x < margin;
  Expr nearRight = x >= width - margin;
  Expr nearBoundary = nearTop || nearBottom || nearLeft || nearRight;

  Func dx, dy, mag("mag");
  dx(x, y) = f(x, y) - f(x-1, y);
  dy(x, y) = f(x, y) - f(x, y-1);
  // TODO: implicit args break this line
  mag(x, y) = select(nearBoundary, 1e10, dx(x, y)*dx(x, y) + dy(x, y)*dy(x, y));

  return mag;
}

Expr argmin3(Expr idx1, Expr val1, Expr idx2, Expr val2, Expr idx3, Expr val3) {
    return select(val1 < val2 && val1 < val3, idx1, select(val2 < val3, idx2, idx3));
}

int main(int argc, char **argv) {

  UniformImage input(UInt(16), 3);

  Var x("x"), y("y"), c("c"), xo("blockidx"), yo("blockidy"), 
    xi("threadidx"), yi("threadidy");

  // The algorithm


  // Add a boundary condition
  Func clamped;
  clamped(x, y, c) = input(clamp(x, 0, input.width()-1), 
                           clamp(y, 0, input.height()-1), c);

  // Convert to floating point
  Func floating("floating");
  floating(x, y, c) = cast<float>(clamped(x, y, c)) / 65535.0f;

  // Convert to grayscale
  Func gray("gray");
  gray(x, y) = floating(x, y, 0) + floating(x, y, 1) + floating(x, y, 2);
  
  Func gradMag = gradientMagnitude(gray, input.width(), input.height());

  RDom yr(0, input.height());
  Func energy("energy");
  Expr xp = min(x+1, input.width()-1);
  Expr xm = max(x-1, 0);
  energy(x, y) = gradMag(x, y);
  energy(x, yr) = gradMag(x, yr) + min(min(energy(xp, yr - 1), 
                                           energy(xm, yr - 1)),
                                       energy(x, yr - 1));
  
  RDom xr(1, input.width() - 1);
  
  // Index of minimum energy on a scanline
  Func minEnergy("minEnergy");
  minEnergy(y) = 0;
  Expr bestSoFar = clamp(minEnergy(y), 0, input.width()-1);
  minEnergy(y) = select(energy(xr, y) < energy(bestSoFar, y), xr, bestSoFar);
 
  // serves as reduction that goes from input.height() - 1 to 0
  Expr flipY = input.height() - yr - 1;

  // calculate location of seam
  Func seam("seam");
  seam(y) = minEnergy(input.height()-1);

  xm = clamp(seam(flipY)-1, 0, input.width()-1);
  xp = clamp(seam(flipY)+1, 0, input.width()-1);
  Expr xh = clamp(seam(flipY), 0, input.width()-1);

  // Follow the path of least energy upwards
  seam(flipY - 1) = argmin3(xm, energy(xm, flipY-1),
                            xh, energy(xh, flipY-1),
                            xp, energy(xp, flipY-1));

  // remove seam
  Func output("output");
  output(x, y, c) = select(x < seam(y), clamped(x, y, c), clamped(x+1, y, c));

  // draws seam
  Expr red = select(c==0, 65535, 0.0f);
  Func seams("seams");
  seams(x, y, c) = select(x==seam(y), red, clamped(x,y,c));

  // schedule
  gradMag.root();
  minEnergy.root();

  output.compileToFile("seam_carving");
  return 0;
}
