#include <Halide.h>
#include "vzero.h"
#include <stdio.h>
using namespace Halide;

// RUN: rm -f vzero.stdout; ./vzero.out; llvm-dis -o vzero.stdout vzero.bc; FileCheck %s < vzero.stdout

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);

  //CHECK: call{{.*}}@llvm.hexagon.V6.vd0
  testBzero<uint32_t>(target);

  printf ("Done\n");

  return 0;
}
