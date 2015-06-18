#include <Halide.h>
#include "halide-hexagon-setup.h"
#include "vmpy.h"
#include <stdio.h>
using namespace Halide;
#ifdef NOSTDOUT
#define OFILE "x.s"
#else
#define OFILE "/dev/stdout"
#endif
#define COMPILE(X)  ((X).compile_to_assembly(OFILE, args, target))
#define COMPILE_BC(X)  ((X).compile_to_bitcode("x.bc", args, target))

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
  return 0;
}
