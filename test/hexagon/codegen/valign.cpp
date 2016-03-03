// RUN: ./valign.out | FileCheck %s
#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);
//CHECK: = vmem(
//CHECK: valign(v{{[0-9]+}},v{{[0-9]+}},#4)
//CHECK-NOT: = vmemu

void check_valign(Target target) {
  Halide::Var x("x"), y("y");
  ImageParam i1 (type_of<int16_t>(), 1);
  ImageParam i2 (type_of<int16_t>(), 1);
  i1.set_min(0, 0);
  i2.set_min(0, 0);
  //  i1.set_stride(, (i1.stride(1)/32) * 32);
  Halide::Func f, g, h;
  f(x) = 3 * i1(x);
  g(x) = 2 * i2(x);
  h(x) = f(x) * g(x+1) * g(x-1);

  std::vector<Argument> args(2);
  args[0]  = i1;
  args[1] = i2;
  f.compute_root();
  g.compute_root();
  h.vectorize(x, 32);
  h.bound(x, 0, (h.output_buffer().width() / 32) * 32);
  COMPILE(h, "_h");
}
int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonTestSetup(target);
  check_valign(target);
  return 0;
}
