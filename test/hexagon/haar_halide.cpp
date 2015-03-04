#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
//RUN: ./haar_halide.out | FileCheck %s

#define COMPILE(X)  ((X).compile_to_assembly("/dev/stdout", args, target))
int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  Halide::Var x("x"), y("y");
  Var xo,xi;

  ImageParam In (type_of<int16_t>(), 2);
  Halide::Func Haar;
  //CHECK-DAG: vnavg(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  //CHECK-DAG: vavg(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  //CHECK-DAG: vsat(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  //CHECK-DAG: vsat(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  Haar(x, y) = select(x < 32, clamp((((In(x, y) + In(x+64,y)) + (In(x+32,y) + In(x+96,y)))/2), 0, 255) |
                      (clamp((((In(x, y) + In(x+64,y)) - (In(x+32,y) + In(x+96,y)))/2), 0, 255) << 8),
                      clamp((((In(x-32, y) - In(x+32,y)) + (In(x,y) - In(x+64,y)))/2), 0, 255) |
                      (clamp((((In(x-32, y) - In(x+32,y)) - (In(x,y) - In(x+64,y)))/2), 0, 255) << 8));

  Haar.bound(x, 0, 64).split(x, xo, xi, 32).vectorize(xi).unroll(xo);

  std::vector<Argument> args(1);
  args[0]  = In;
  COMPILE(Haar);
  printf ("Done\n");
  return 0;
}
