#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128
// RUN: ./vmpyi-vector-by-scalar.out | FileCheck %s

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonTestSetup(target);
  //CHECK: vmpyi(v{{[0-9]+}}.w,r{{[0-9]+}}.h)
  Halide::Var x("x");
  ImageParam i1 (type_of<int32_t>(), 1);
  Halide::Func F;
  F(x) = i1(x) + 252 *i1(x+1);
  F.vectorize(x, 32);
  std::vector<Argument> args(1);
  args[0]  = i1;
  COMPILE(F, "vmpyiF");
  return 0;
}
