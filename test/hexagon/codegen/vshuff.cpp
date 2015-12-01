#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

// RUN: ./vshuff.out | FileCheck %s
//CHECK: vshuffo(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
//CHECK: vshuffe(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
void testVshuffo(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(1);
  Halide::Func Shuffo;
  ImageParam InputOne (type_of<uint8_t>(), 1);
  Halide::Func In_16;
  In_16(x) = cast<uint16_t>(InputOne(x));
  Shuffo(x) = cast<uint8_t>(In_16(x) >> 8);
  Shuffo.vectorize(x, 64) ;
  args[0]  = InputOne;
  COMPILE(Shuffo, "VShuffo");
}
void testVshuffe(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(1);
  Halide::Func Shuffe;
  ImageParam InputOne (type_of<uint8_t>(), 1);
  Halide::Func In_16;
  In_16(x) = cast<uint16_t>(InputOne(x));
  Shuffe(x) = cast<int8_t>(In_16(x));
  Shuffe.vectorize(x, 64);
  args[0]  = InputOne;
  COMPILE(Shuffe, "VShuffe");
}

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  commonTestSetup(target);
  testVshuffo(target);
  testVshuffe(target);
  printf ("Done\n");
  return 0;
}
