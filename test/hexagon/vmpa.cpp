#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
#ifdef NOSTDOUT
#define OFILE "x.s"
#else
#define OFILE "/dev/stdout"
#endif
#define COMPILE(X)  ((X).compile_to_assembly(OFILE, args, target))
#define COMPILE_BC(X)  ((X).compile_to_bitcode("x.bc", args, target))

#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

// RUN: ./vmpa.out | FileCheck %s
// CHECK: testOne,@function
// CHECK: vmpa(v{{[0-9]+}}:{{[0-9]+}}.ub,v{{[0-9]+}}:{{[0-9]+}}.ub)
void testOne(Target &target) {
  Halide::Var x("x"), y("y");
  Func Result("testOne");
  ImageParam inputOne (type_of<uint8_t>(), 1);
  ImageParam inputTwo (type_of<uint8_t>(), 1);
  Func A16, B16;
  A16(x) = cast<int16_t>(inputOne(x));
  B16(x) = cast<int16_t>(inputTwo(x));
  Result(x)= (A16(2*x) * B16(2*x)) + (B16(2*x+1) * A16(2*x+1));
  Result.vectorize(x, 64);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(Result);
}
// CHECK: testTwo,@function
// CHECK: vmpa(v{{[0-9]+}}:{{[0-9]+}}.ub,v{{[0-9]+}}:{{[0-9]+}}.ub)
void testTwo(Target &target) {
  Halide::Var x("x"), y("y");
  Func Result("testTwo");
  ImageParam inputOne (type_of<uint8_t>(), 1);
  Func A16, B16;
  A16(x) = cast<int16_t>(inputOne(x));
  Result(x)= (A16(2*x) * 5) + (3 * A16(2*x+1));
  Result.vectorize(x, 64);
  std::vector<Argument> args(1);
  args[0]  = inputOne;
  COMPILE(Result);
}
int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  testOne(target);
  testTwo(target);
  return 0;
}
