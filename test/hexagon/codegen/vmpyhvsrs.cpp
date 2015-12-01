#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

// RUN: ./vmpyhvsrs.out | FileCheck %s
//CHECK: __test_vmpyhvsrs
//CHECK: vmpy(v{{[0-9]+}}.h,v{{[0-9]+}}.h):<<1:rnd:sat
void test_vmpyhvsrs(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f(type_of<uint8_t>(), 1);
  ImageParam g(type_of<uint8_t>(), 1);
  F(x) = cast<int16_t> (f(x));
  G(x) = cast<int16_t> (g(x));

  res(x) = cast<uint8_t>(usat_8(cast<int32_t>(F(x)) * cast<int32_t>(G(x)) + (1<<14) >> 15));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_vmpyhvsrs");
}


int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  commonTestSetup(target);
  test_vmpyhvsrs(target);
  printf ("Done\n");
  return 0;
}
