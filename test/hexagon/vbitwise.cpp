#include <Halide.h>
#include "halide-hexagon-setup.h"
#include "vbitwise.h"
#include <stdio.h>
using namespace Halide;

// RUN: ./vbitwise.out | FileCheck %s

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  // CHECK: vor(v{{[0-9]+}},v{{[0-9]+}})
  testOr<uint8_t>(target);
  // CHECK: vand(v{{[0-9]+}},v{{[0-9]+}})
  testAnd<int16_t>(target);
  // CHECK: vxor(v{{[0-9]+}},v{{[0-9]+}})
  testXor<uint16_t>(target);
  // CHECK: vnot(v{{[0-9]+}})
  testNot<int32_t>(target);

  printf ("Done\n");

  return 0;
}
