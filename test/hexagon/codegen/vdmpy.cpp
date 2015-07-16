#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

// RUN: ./vdmpy.out | FileCheck %s
// CHECK: testOne,@function
//CHECK: vdmpy(v{{[0-9]+}}.h,v{{[0-9]+}}.h):sat

void testOne(Target &target) {
  Halide::Var x("x"), y("y");
  Func Result("testOne");
  ImageParam inputOne (type_of<int16_t>(), 1);
  ImageParam inputTwo (type_of<int16_t>(), 1);
  Func A16, B16;
  A16(x) = cast<int32_t>(inputOne(x));
  B16(x) = cast<int32_t>(inputTwo(x));
  Result(x)=  sat_i32((A16(2*x) * B16(2*x)) + (B16(2*x+1) * A16(2*x+1)));
  Result.vectorize(x, 16);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(Result, "testOne");
}
// CHECK: testTwo,@function
//CHECK: vdmpy(v{{[0-9]+}}.h,v{{[0-9]+}}.h):sat
void testTwo(Target &target) {
  Halide::Var x("x"), y("y");
  Func Result("testTwo");
  ImageParam inputOne (type_of<int16_t>(), 1);
  Func A32;
  A32(x) = cast<int32_t>(inputOne(x));
  Result(x)= (A32(2*x) * 5) + (3 * A32(2*x+1));
  Result.vectorize(x, 16);
  std::vector<Argument> args(1);
  args[0]  = inputOne;
  COMPILE(Result, "testTwo");
}
int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  testOne(target);
  testTwo(target);
  return 0;
}
