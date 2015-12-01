#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

// RUN: ./vasr.out | FileCheck %s


//CHECK: __test_vasr
//CHECK: vasr(v{{[0-9]+}}.h,v{{[0-9]+}}.h,r{{[0-7]+}}):sat
void test_vasr(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f (type_of<uint8_t>(), 1);
  ImageParam g (type_of<uint8_t>(), 1);
  res(x) = cast<uint8_t>(usat_8(cast<int16_t>(f(x)) + cast<int16_t>(g(x)) >> 4));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_vasr");
}
int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  commonTestSetup(target);
  test_vasr(target);
  printf ("Done\n");
  return 0;
}
