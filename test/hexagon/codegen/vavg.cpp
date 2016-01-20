#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

// RUN: ./vavg.out | FileCheck %s
// CHECK: vavg(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub):rnd
// CHECK: vavg(v{{[0-9]+}}.uh,v{{[0-9]+}}.uh):rnd

// Average two positive values rounding up
Expr avg(Expr a, Expr b) {
    Type wider = a.type().with_bits(a.type().bits() * 2);
    return cast(a.type(), (cast(wider, a) + b + 1)/2);
}

void testVAVG_u8(Target& target) {
  Halide::Var x("x"), y("y");
  std::vector<Argument> args(2);
  Halide::Func vavg_u8;
  ImageParam i1 (type_of<uint8_t>(), 1);
  ImageParam i2 (type_of<uint8_t>(), 1);
  vavg_u8(x) = avg(i1(x), i2(x));
  vavg_u8.vectorize(x, 64);
  args[0]  = i1;
  args[1] = i2;
  COMPILE(vavg_u8, "vavg_u8");
}
void testVAVG_u16(Target& target) {
  Halide::Var x("x"), y("y");
  std::vector<Argument> args(2);
  Halide::Func vavg_u16;
  ImageParam i1 (type_of<uint16_t>(), 1);
  ImageParam i2 (type_of<uint16_t>(), 1);
  vavg_u16(x) = avg(i1(x), i2(x));
  vavg_u16.vectorize(x, 32);
  args[0]  = i1;
  args[1] = i2;
  COMPILE(vavg_u16, "vavg_u16");
}

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonTestSetup(target);
  testVAVG_u8(target);
  testVAVG_u16(target);
  printf ("Done\n");
  return 0;
}
