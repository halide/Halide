#include <Halide.h>
#include "halide-hexagon-setup.h"
#include "vext.h"
#include <stdio.h>
using namespace Halide;

// RUN: ./vext.out | FileCheck %s

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);

  //CHECK: vzxt(v{{[0-9]+}}.ub)
  testZeroExtend<uint8_t>(target);
  //CHECK: vzxt(v{{[0-9]+}}.uh)
  testZeroExtend<uint16_t>(target);
  //CHECK: vsxt(v{{[0-9]+}}.b)
  testSignExtend<int8_t>(target);
  //CHECK: vsxt(v{{[0-9]+}}.h)
  testSignExtend<int16_t>(target);
  printf ("Done\n");
  return 0;
}
