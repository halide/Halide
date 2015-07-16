#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

// RUN: ./vmpa.out | FileCheck %s
// CHECK: testOne,@function
// CHECK: vmpa(v{{[0-9]+}}:{{[0-9]+}}.ub,v{{[0-9]+}}:{{[0-9]+}}.ub)
void testOne(Target &target, bool isDbl) {
  Halide::Var x("x"), y("y");
  Func Result("testOne");
  ImageParam inputOne (type_of<uint8_t>(), 1);
  ImageParam inputTwo (type_of<uint8_t>(), 1);
  Func A16, B16;
  A16(x) = cast<int16_t>(inputOne(x));
  B16(x) = cast<int16_t>(inputTwo(x));
  Result(x)= (A16(2*x) * B16(2*x)) + (B16(2*x+1) * A16(2*x+1));
  Result.vectorize(x, isDbl ? 128 : 64);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(Result, "testOne");
}
// CHECK: testTwo,@function
// CHECK: vmpa(v{{[0-9]+}}:{{[0-9]+}}.ub,v{{[0-9]+}}:{{[0-9]+}}.ub)
void testTwo(Target &target, bool isDbl) {
  Halide::Var x("x"), y("y");
  Func Result("testTwo");
  ImageParam inputOne (type_of<uint8_t>(), 1);
  Func A16, B16;
  A16(x) = cast<int16_t>(inputOne(x));
  Result(x)= (A16(2*x) * 5) + (3 * A16(2*x+1));
  Result.vectorize(x, isDbl ? 128 : 64);
  std::vector<Argument> args(1);
  args[0]  = inputOne;
  COMPILE(Result, "testTwo");
}
int main(int argc, char **argv) {
  Target target;
  bool isDbl = false;
  setupHexagonTarget(target);
  if (argc>1) {
   target.set_feature(Target::HVX_DOUBLE);
   isDbl = true;
  }
  testOne(target,isDbl);
  testTwo(target,isDbl);
  return 0;
}
