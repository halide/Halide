#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);
// RUN: ./vabsdiff.out | FileCheck %s
//CHECK: vabsdiff(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub)
void testVabsdiff(Target& target) {
  Halide::Var x("x"), y("y");
  std::vector<Argument> args(2);
  Halide::Func absdiff;
  ImageParam u8_a (type_of<uint8_t>(), 1);
  ImageParam u8_b (type_of<uint8_t>(), 1);
  absdiff(x) = absd(u8_a(x), u8_b(x));
  absdiff.vectorize(x, 64);
  args[0] = u8_a;
  args[1] = u8_b;
  COMPILE(absdiff, "Vabsdiff");
}
int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  testVabsdiff(target);
  printf ("Done\n");
  return 0;
}
