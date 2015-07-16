#include <Halide.h>
#include "vmpy.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128
// RUN: ./vmpy.out | FileCheck %s


int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  //CHECK: vmpy(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub)
  testVMPY<uint8_t, uint8_t, uint16_t>(target);
  //CHECK: vmpy(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
  testVMPY<int16_t, int16_t, int32_t>(target);
  //CHECK: vmpy(v{{[0-9]+}}.ub,r{{[0-9]+}}.b)
  testWideningMultiply<uint8_t, int16_t>(target);
  // CHECK: vmpy(v{{[0-9]+}}.h,r{{[0-9]+}}.h)
  testWideningMultiply<int16_t, int32_t>(target);
  //CHECK: vmpy(v{{[0-9]+}}.ub,r{{[0-9]+}}.ub)
  testWideningMultiply<uint8_t, uint16_t>(target);
  return 0;
}
