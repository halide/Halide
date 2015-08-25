#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;

// RUN: ./vmpa-accumulate.out | FileCheck %s
// CHECK: testOne,@function
// CHECK: += vmpa(v{{[0-9]+}}:{{[0-9]+}}.ub,r{{[0-9]+}}.b)
void testOne(Target &target, bool isDbl) {
  Halide::Var x("x"), y("y");
  Func Result("testOne");
  ImageParam inputOne (type_of<uint8_t>(), 1);
  Func A16, B16;
  A16(x) = cast<int16_t>(inputOne(x));
  Result(x)= A16(x) + A16(x+1) + 3*A16(x+2);
  Result.vectorize(x, 128);
  std::vector<Argument> args(1);
  args[0]  = inputOne;
  COMPILE(Result, "testOne");
}
int main(int argc, char **argv) {
  Target target;
  bool isDbl = false;
  setupHexagonTarget(target);
     target.set_feature(Target::HVX_DOUBLE);
  testOne(target,isDbl);
  return 0;
}
