// RUN: ./vselect.out | FileCheck %s
#include <Halide.h>
#include "vselect.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonTestSetup(target);
  /* Test variants of vector add */
  //CHECK: vcmp.gt(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
  //CHECK: vmux(q{{[0-3]+}},v{{[0-9]+}},v{{[0-9]+}})
  testSelectLessThan<int8_t>(target);
  //CHECK: vcmp.gt(v{{[0-9]+}}.uh,v{{[0-9]+}}.uh)
  //CHECK: vmux(q{{[0-3]+}},v{{[0-9]+}},v{{[0-9]+}})
  testSelectLessThan<uint16_t>(target);

  // select( a != b, A, B) gets converted into
  // select( a == b, B, A)
  //CHECK: vcmp.eq(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
  testSelectNotEqual<uint8_t>(target);
  //CHECK: vcmp.eq(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
  //CHECK: vmux(q{{[0-3]+}},v{{[0-9]+}},v{{[0-9]+}})
  testSelectNotEqual<int32_t>(target);
  //CHECK: Narrow
  //CHECK: [[RES:v[0-9]+]] = vmux(
  //CHECK: vzxt([[RES]].ub)
  testSelectNarrowing<uint8_t, uint16_t>(target);
  printf ("Done\n");

  return 0;
}
