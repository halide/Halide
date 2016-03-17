// RUN: ./interleave_vectors.out | FileCheck %s
#include <Halide.h>
#include "interleave_vectors.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);


int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonPerfSetup(target);

  // CHECK: __check_int8_t
  // CHECK: v{{[0-9]+}}:{{[0-9]+}} = vshuff(v{{[0-9]+}},v{{[0-9]+}},r{{[0-9]+}})
  check_interleave<int8_t>(target, "check_int8_t");

  // CHECK: __check_uint8_t
  // CHECK: v{{[0-9]+}}:{{[0-9]+}} = vshuff(v{{[0-9]+}},v{{[0-9]+}},r{{[0-9]+}})
  check_interleave<uint8_t>(target, "check_uint8_t");

  // CHECK: __check_int16_t
  // CHECK: v{{[0-9]+}}:{{[0-9]+}} = vshuff(v{{[0-9]+}},v{{[0-9]+}},r{{[0-9]+}})
  check_interleave<int16_t>(target, "check_int16_t");

  // CHECK: __check_uint16_t
  // CHECK: v{{[0-9]+}}:{{[0-9]+}} = vshuff(v{{[0-9]+}},v{{[0-9]+}},r{{[0-9]+}})
  check_interleave<uint16_t>(target, "check_uint16_t");

  // CHECK: __check_int32_t
  // CHECK: v{{[0-9]+}}:{{[0-9]+}} = vshuff(v{{[0-9]+}},v{{[0-9]+}},r{{[0-9]+}})
  check_interleave<int32_t>(target, "check_int32_t");

  // CHECK: __check_uint32_t
  // CHECK: v{{[0-9]+}}:{{[0-9]+}} = vshuff(v{{[0-9]+}},v{{[0-9]+}},r{{[0-9]+}})
  check_interleave<uint32_t>(target, "check_uint32_t");

  return 0;
}
