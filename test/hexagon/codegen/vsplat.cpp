#include <Halide.h>
#include "vsplat.h"
#include <stdio.h>
using namespace Halide;

// RUN: ./vsplat.out | FileCheck %s

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonTestSetup(target);

  /* Test variants of vector splat */
  //CHECK: v{{[0-9]+}}{{[ ]*}}={{[ ]*}}vsplat
  testBcast<int8_t>(target);
  //CHECK: v{{[0-9]+}}{{[ ]*}}={{[ ]*}}vsplat
  testBcast<uint8_t>(target);

  //CHECK: v{{[0-9]+}}{{[ ]*}}={{[ ]*}}vsplat
  testBcast<int16_t>(target);
  //CHECK: v{{[0-9]+}}{{[ ]*}}={{[ ]*}}vsplat
  testBcast<uint16_t>(target);

  //CHECK: v{{[0-9]+}}{{[ ]*}}={{[ ]*}}vsplat
  testBcast<int32_t>(target);
  //CHECK: v{{[0-9]+}}{{[ ]*}}={{[ ]*}}vsplat
  testBcast<uint32_t>(target);


  setupHexagonTarget(target, Target::HVX_64);
  target.set_feature(Target::HVX_V62);

  /* Test variants of vector splat */
  //CHECK: v{{[0-9]+}}.b{{[ ]*}}={{[ ]*}}vsplat
  testBcast<int8_t>(target);
  //CHECK: v{{[0-9]+}}.b{{[ ]*}}={{[ ]*}}vsplat
  testBcast<uint8_t>(target);

  //CHECK: v{{[0-9]+}}.h{{[ ]*}}={{[ ]*}}vsplat
  testBcast<int16_t>(target);
  //CHECK: v{{[0-9]+}}.h{{[ ]*}}={{[ ]*}}vsplat
  testBcast<uint16_t>(target);

  //CHECK: v{{[0-9]+}}{{[ ]*}}={{[ ]*}}vsplat
  testBcast<int32_t>(target);
  //CHECK: v{{[0-9]+}}{{[ ]*}}={{[ ]*}}vsplat
  testBcast<uint32_t>(target);

  printf ("Done\n");

  return 0;
}
