#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;

#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128
// RUN: ./vmpyi.out | FileCheck %s

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  //CHECK: vmpyi(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  Halide::Var x("x");
  ImageParam i1 (type_of<int16_t>(), 1);
  ImageParam i2 (type_of<int16_t>(), 1);
  Halide::Func F;
  F(x) = i1(x) * i2(x);
  int vector_factor = VECTORSIZE / sizeof(int16_t);
  F.vectorize(x, vector_factor);
  std::vector<Argument> args(2);
  args[0]  = i1;
  args[1] = i2;
  COMPILE(F, "vmpyiF");
  return 0;
}
