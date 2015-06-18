#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;

// RUN: ./vsat.out | FileCheck %s
#ifdef NOSTDOUT
#define OFILE "x.s"
#else
#define OFILE "/dev/stdout"
#endif
#define COMPILE(X)  ((X).compile_to_assembly(OFILE, args, target))
//CHECK: vsat(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
void testVsat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func VsatTest;
  ImageParam InputOne (type_of<int32_t>(), 2);
  ImageParam InputTwo (type_of<int32_t>(), 2);
  VsatTest (x, y) = clamp(InputOne(x,y), -32768, 32767) | (clamp (InputTwo(x, y), -32768, 32767) << 16);
  VsatTest.split(x, x_outer, x_inner, 16);
  VsatTest.vectorize(x_inner);
  args[0]  = InputOne;
  args[1] = InputTwo;
  COMPILE(VsatTest);
}

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  testVsat(target);
  printf ("Done\n");
  return 0;
}
