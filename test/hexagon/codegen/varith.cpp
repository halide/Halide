#include <Halide.h>
#include "varith.h"
#include <stdio.h>
using namespace Halide;

// RUN: ./varith.out | FileCheck %s

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonTestSetup(target);

  /* Test variants of vector add */
  //CHECK: vadd(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
  testAdd<int8_t>(target);
  //CHECK: vadd(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub):sat
  testAdd<uint8_t>(target);

  //CHECK: vadd(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  testAdd<int16_t>(target);
  //CHECK: vadd(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  testAdd<uint16_t>(target);

  //CHECK: vadd(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
  testAdd<int32_t>(target);
  // Commenting these two tests out until bug 20163 is fixed.

  //CHECK: vadd(v{{[0-9]+}}:{{[0-9]+}}.b,v{{[0-9]+}}:{{[0-9]+}}.b)
  testAddDouble<int8_t>(target);
  //CHECK: vadd(v{{[0-9]+}}:{{[0-9]+}}.ub,v{{[0-9]+}}:{{[0-9]+}}.ub):sat
  testAddDouble<uint8_t>(target);
  // Commenting these two tests out until bug 20163 is fixed.
  // //CHECK_PDB: vadd(v{{[0-9]+}}:{{[0-9]+}}.h,v{{[0-9]+}}:{{[0-9]+}}.h)
  // testAddDouble<int16_t>(target);
  // //CHECK_PDB: vadd(v{{[0-9]+}}:{{[0-9]+}}.uh,v{{[0-9]+}}:{{[0-9]+}}.uh):sat
  // testAddDouble<uint16_t>(target);

  //CHECK: vadd(v{{[0-9]+}}:{{[0-9]+}}.w,v{{[0-9]+}}:{{[0-9]+}}.w)
  testAddDouble<int32_t>(target);

  /* Test variants of vector sub */
  //CHECK: vsub(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
  testSub<int8_t>(target);
  //CHECK: vsub(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub)
  testSub<uint8_t>(target);

  //CHECK: vsub(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  testSub<int16_t>(target);
  //CHECK: vsub(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  testSub<uint16_t>(target);

  //CHECK: vsub(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
  testSub<int32_t>(target);

  //CHECK: vsub(v{{[0-9]+}}:{{[0-9]+}}.b,v{{[0-9]+}}:{{[0-9]+}}.b)
  testSubDouble<int8_t>(target);
  //CHECK: vsub(v{{[0-9]+}}:{{[0-9]+}}.ub,v{{[0-9]+}}:{{[0-9]+}}.ub):sat
  testSubDouble<uint8_t>(target);
  // Commenting these two tests out until bug 20163 is fixed.
  // //CHECK_PDB: vsub(v{{[0-9]+}}:{{[0-9]+}}.h,v{{[0-9]+}}:{{[0-9]+}}.h)
  // testSubDouble<int16_t>(target);
  // //CHECK_PDB: vsub(v{{[0-9]+}}:{{[0-9]+}}.uh,v{{[0-9]+}}:{{[0-9]+}}.uh):sat
  // testSubDouble<uint16_t>(target);

  //CHECK: vsub(v{{[0-9]+}}:{{[0-9]+}}.w,v{{[0-9]+}}:{{[0-9]+}}.w)
  testSubDouble<int32_t>(target);

  //CHECK: vavg(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub)
  //CHECK: vnavg(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub)
  testAvg<uint8_t>(target);
  //CHECK: vavg(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
  //CHECK: vnavg(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
  testAvg<int32_t>(target);
  //Todo: testAvg for 'h'
  // no vnavg for uh.
  printf ("Done\n");

  return 0;
}
