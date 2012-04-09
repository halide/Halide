#include <Halide.h>
using namespace Halide;

#include "../png.h"

#define PI 3.14159

Func gradientMagnitude(Func f, Expr width, Expr height) {

  Var x, y, c, a, b;

  Expr isTop = y <= 10;
  Expr isBottom = y >= height - 10;
  Expr isLeft = x <= 10;
  Expr isRight = x >= width - 10;

  Func dI2("dI2");
  dI2(x, y, a, b) = pow(f(x, y, 0) - f(x+a, y+b, 0), 2) +
    pow(f(x, y, 1) - f(x+a, y+b, 1), 2) +
    pow(f(x, y, 1) - f(x+a, y+b, 1), 2);

  Func dI("dI");
  dI(x, y, a, b) = sqrt(dI2(x, y, a, b));

  Func gradient2D("gradient2D");
  gradient2D(x, y) =
    select(isTop, 0.0f, 1.0f)*dI(x, y, 0, -1) +
    select(isBottom, 0.0f, 1.0f)*dI(x, y, 0, 1) +
    select(isLeft, 0.0f, 1.0f)*dI(x, y, -1, 0) +
    select(isRight, 0.0f, 1.0f)*dI(x, y, 1, 0);

  Func norm("norm");
  norm(x, y) =
    select(isTop, 0.0f, 1.0f) +
    select(isBottom, 0.0f, 1.0f) +
    select(isLeft, 0.0f, 1.0f) +
    select(isRight, 0.0f, 1.0f);

  Func gradient("gradient");
  gradient(x, y, c) = gradient2D(x, y)/norm(x, y);

  return gradient;
}

int main(int argc, char **argv) {

  //  Uniform<int> k;
  UniformImage input(UInt(16), 3);

  Var x("x"), y("y"), c("c"), xo("blockidx"), yo("blockidy"), 
    xi("threadidx"), yi("threadidy");

  // The algorithm


  Func clampH("clampH");
  clampH(y) = clamp(y, 0, input.height() - 1);
  Func clampW("clampW");
  clampW(x) = clamp(x, 0, input.width() - 1);



  // Convert to floating point
  Func floating("floating");
  floating(x, y, c) = cast<float>(input(x, y, c)) / 65535.0f;
  // Set a boundary condition
  Func clamped("clamped");
  clamped(x, y, c) = floating(clamp(x, 0, input.width()-1), 
			      clamp(y, 0, input.height()-1), c);
  
  Func g;
  g = gradientMagnitude(clamped, input.width(), input.height());

  RVar yr(0, input.height());
  Func energy("energy");
  energy(x, y, c) = g(x, y, c);
  energy(x, yr, c) = g(x, yr, c) + min(min(energy(x, yr - 1, c), 
					   energy(clampW(x - 1), yr - 1, c)),
				       energy(clampW(x + 1), yr - 1, c));
  // important for correctness that we calculate row by row
  energy.update().transpose(yr, x); 
  
  RVar xr(1, input.width() - 1);
  
  Func minEnergy("minEnergy");
  minEnergy(y) = 0;
  minEnergy(y) = 
    select(energy(xr, y, 0) < 
	   energy(clampW(minEnergy(y)), y, 0), 
	   xr, minEnergy(y));
  
  // serves as reduction that goes from input.height() - 1 to 0
  Expr flipY = input.height() - yr - 1;

  // calculate location of seam
  Func seam("seam");
  seam(y) = minEnergy(y);
  seam(flipY - 1) = select(select( energy( clampW(seam(flipY) - 1), flipY - 1, 0) < energy( clampW(seam(flipY) + 1), flipY - 1, 0),
				  energy( clampW(seam(flipY) - 1), flipY - 1, 0),
				  energy( clampW(seam(flipY) + 1), flipY - 1, 0)
				  ) < 
			  energy( clampW(seam(flipY)), flipY - 1, 0),
			  select( energy( clampW(seam(flipY) - 1), flipY - 1, 0) < energy( clampW(seam(flipY) + 1), flipY - 1, 0),
				  clampW(seam(flipY) - 1),
				  clampW(seam(flipY) + 1)
				  ),
			  clampW(seam(flipY))
			  );
  
  // remove seam
  Func output("output");
  output(x, y, c) = clamped(x, y, c);
  output(x, yr, c) = select(x < seam(yr), clamped(x, yr, c), 
			    select(x + 1 < input.width(), clamped(x + 1, yr, c), 
				   select( yr%2==0, 0.0f, 1.0f )));

  // draws seam
  Func seams("seams");
  seams(x, y, c) = clamped(x, y, c);
  seams(x, yr, c) = select(x==seam(yr), select(c==0, 1.0f, 0.0f),
			   clamped(x,yr,c));

  // Convert back to 16-bit
  Func outputClamped("outputClamped");
  outputClamped(x, y, c) = cast<uint16_t>(output(x, y, c) * 65535.0f);

  // schedule

  outputClamped.compileToFile("seam_carving");
  return 0;
}
