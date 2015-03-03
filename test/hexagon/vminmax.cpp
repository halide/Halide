#include <Halide.h>
#include "halide-hexagon-setup.h"
#include "vminmax.h"
#include <stdio.h>
using namespace Halide;

// RUN: ./vminmax.out | FileCheck %s

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);

  //CHECK: vmax(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub)
  testMax<uint8_t>(target);
  //CHECK: vmax(v{{[0-9]+}}.uh,v{{[0-9]+}}.uh)
  testMax<uint16_t>(target);
  //CHECK: vmax(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  testMax<int16_t>(target);
  //CHECK: vmax(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
  testMax<int32_t>(target);
  //CHECK: vmin(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub)
  testMin<uint8_t>(target);
  //CHECK: vmin(v{{[0-9]+}}.uh,v{{[0-9]+}}.uh)
  testMin<uint16_t>(target);
  //CHECK: vmin(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  testMin<int16_t>(target);
  //CHECK: vmin(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
  testMin<int32_t>(target);

  printf ("Done\n");

  return 0;
}
